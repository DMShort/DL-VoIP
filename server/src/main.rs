use std::sync::Arc;

use tokio::net::TcpListener;
use tokio::time::Instant;
use tracing::{error, info, warn};
use axum_server::tls_rustls::RustlsConfig;

use voip_server::auth::session::{self, SessionCache};
use voip_server::bootstrap;
use voip_server::config::AppConfig;
use voip_server::crypto::session_manager::SrtpSessionManager;
use voip_server::db;
use voip_server::server::{self, AppState};
use voip_server::state::channel_manager::ChannelManager;
use voip_server::state::connection_manager::ConnectionManager;
use voip_server::state::presence::PresenceManager;
use voip_server::voice::buffer_pool::BufferPool;
use voip_server::voice::rate_limiter::VoiceRateLimiter;

const VERSION: &str = env!("CARGO_PKG_VERSION");

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    eprintln!("DadLink Voice Server starting...");

    // Initialize logging
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("voip_server=info")),
        )
        .with_target(true)
        .init();

    // Load configuration
    let config = AppConfig::load()?;

    // Startup banner
    info!("╔══════════════════════════════════════════╗");
    info!("║         DadLink Voice Server v{}       ║", VERSION);
    info!("╚══════════════════════════════════════════╝");
    info!(
        "Control port: {}:{}",
        config.server.bind_address, config.server.control_port
    );
    info!(
        "Voice port:   {}:{}",
        config.server.bind_address, config.server.voice_port
    );
    info!(
        "Database:     {}",
        mask_password(&config.database.url)
    );

    if config.server.tls_cert_path.is_some() && config.server.tls_key_path.is_some() {
        info!("TLS:          enabled");
    } else {
        warn!("TLS:          disabled (no cert/key configured)");
    }

    if config.security.jwt_secret == "CHANGE_ME_IN_PRODUCTION" {
        warn!("JWT secret is using default value — change this in production!");
    }

    // Initialize database
    let pool = db::init_pool(&config.database.url, config.database.max_connections).await?;
    db::run_migrations(&pool).await?;

    // Run bootstrap if needed
    if let Err(e) = bootstrap::run_bootstrap(&pool, &config.bootstrap).await {
        error!("Bootstrap failed: {}", e);
        return Err(e);
    }

    // Initialize in-memory managers
    let session_cache = Arc::new(SessionCache::new(5));
    let channel_manager = Arc::new(ChannelManager::new());
    let connection_manager = Arc::new(ConnectionManager::new());
    let presence = Arc::new(PresenceManager::new());
    let srtp_manager = Arc::new(SrtpSessionManager::new());

    // Build application state
    let state = AppState {
        pool: pool.clone(),
        config: Arc::new(config.clone()),
        session_cache,
        channel_manager,
        connection_manager,
        presence,
        srtp_manager,
        start_time: Instant::now(),
    };

    // Build HTTP router
    let app = server::build_router(state.clone());

    // Start session cleanup background task
    tokio::spawn(session::session_cleanup_task(pool.clone()));

    // Start UDP voice server
    let voice_bind = format!("{}:{}", config.server.bind_address, config.server.voice_port);
    let rate_limiter = Arc::new(VoiceRateLimiter::new(config.voice.max_packet_rate));
    let buffer_pool = Arc::new(BufferPool::new(config.voice.buffer_pool_size, 1500));

    tokio::spawn(voip_server::voice::router::run_voice_server(
        voice_bind,
        state.srtp_manager.clone(),
        state.channel_manager.clone(),
        state.connection_manager.clone(),
        rate_limiter,
        buffer_pool,
    ));

    // Start key rotation background task
    tokio::spawn(voip_server::crypto::key_rotation::key_rotation_task(
        config.security.key_rotation_mins,
        state.srtp_manager.clone(),
        state.connection_manager.clone(),
    ));

    // Start rate limiter cleanup task (every 5 minutes)
    tokio::spawn(async {
        let mut timer = tokio::time::interval(std::time::Duration::from_secs(300));
        loop {
            timer.tick().await;
            voip_server::server::rate_limit::auth_limiter().cleanup();
            voip_server::server::rate_limit::admin_limiter().cleanup();
        }
    });

    // Start HTTP/HTTPS server
    let bind_addr = format!("{}:{}", config.server.bind_address, config.server.control_port);

    match (&config.server.tls_cert_path, &config.server.tls_key_path) {
        (Some(cert), Some(key)) => {
            let tls_config = RustlsConfig::from_pem_file(cert, key).await
                .map_err(|e| anyhow::anyhow!("Failed to load TLS certificate: {}", e))?;

            let addr: std::net::SocketAddr = bind_addr.parse()?;
            info!("Server listening on {} (TLS)", bind_addr);

            axum_server::bind_rustls(addr, tls_config)
                .serve(app.into_make_service_with_connect_info::<std::net::SocketAddr>())
                .await?;
        }
        _ => {
            let listener = TcpListener::bind(&bind_addr).await?;
            info!("Server listening on {}", bind_addr);

            axum::serve(
                listener,
                app.into_make_service_with_connect_info::<std::net::SocketAddr>(),
            )
            .with_graceful_shutdown(shutdown_signal())
            .await?;
        }
    }

    info!("Server shut down gracefully.");
    Ok(())
}

async fn shutdown_signal() {
    tokio::signal::ctrl_c()
        .await
        .expect("Failed to listen for ctrl-c");
    info!("Shutdown signal received, draining connections (10s timeout)...");

    // Give existing connections 10 seconds to drain gracefully.
    // axum::serve will stop accepting new connections immediately but
    // existing handlers get this window to finish.
    tokio::time::sleep(std::time::Duration::from_secs(10)).await;
    info!("Drain timeout reached, forcing shutdown.");
}

/// Mask the password portion of a database URL for logging
fn mask_password(url: &str) -> String {
    if let Some(at_pos) = url.find('@') {
        if let Some(colon_pos) = url[..at_pos].rfind(':') {
            let prefix = &url[..colon_pos + 1];
            let suffix = &url[at_pos..];
            return format!("{}****{}", prefix, suffix);
        }
    }
    url.to_string()
}
