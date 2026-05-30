use std::sync::Arc;

use tokio::net::TcpListener;
use tokio::time::Instant;
use tracing::{error, info, warn};
use hyper_util::rt::{TokioExecutor, TokioIo};
use hyper_util::server::conn::auto::Builder as ConnBuilder;
use tokio_rustls::TlsAcceptor;
use tokio_rustls::rustls::ServerConfig as TlsServerConfig;
use hyper_util::service::TowerToHyperService;

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

    match (&config.server.tls_cert_path.clone(), &config.server.tls_key_path.clone()) {
        (Some(cert_path), Some(key_path)) => {
            let acceptor = build_tls_acceptor(cert_path, key_path)?;
            let listener = TcpListener::bind(&bind_addr).await?;
            info!("Server listening on {} (TLS)", bind_addr);

            loop {
                let (tcp_stream, remote_addr) = tokio::select! {
                    res = listener.accept() => res?,
                    _ = shutdown_signal() => break,
                };

                let acceptor = acceptor.clone();
                let app = app.clone();

                tokio::spawn(async move {
                    let tls_stream = match acceptor.accept(tcp_stream).await {
                        Ok(s) => s,
                        Err(e) => { tracing::debug!("TLS accept error: {e}"); return; }
                    };

                    // Wrap router as a hyper service, injecting the peer addr as ConnectInfo
                    let svc = TowerToHyperService::new(tower::service_fn(
                        move |req: axum::http::Request<hyper::body::Incoming>| {
                            let (mut parts, body) = req.into_parts();
                            parts.extensions.insert(axum::extract::ConnectInfo(remote_addr));
                            let req = axum::http::Request::from_parts(
                                parts, axum::body::Body::new(body),
                            );
                            let mut app = app.clone();
                            async move { tower::Service::call(&mut app, req).await }
                        },
                    ));

                    if let Err(e) = ConnBuilder::new(TokioExecutor::new())
                        .serve_connection_with_upgrades(TokioIo::new(tls_stream), svc)
                        .await
                    {
                        tracing::debug!("Connection error: {e}");
                    }
                });
            }
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

fn build_tls_acceptor(cert_path: &str, key_path: &str) -> anyhow::Result<TlsAcceptor> {
    use rustls_pemfile::{certs, private_key};
    use std::io::BufReader;

    let cert_file = std::fs::File::open(cert_path)
        .map_err(|e| anyhow::anyhow!("Cannot open cert file {cert_path}: {e}"))?;
    let cert_chain: Vec<_> = certs(&mut BufReader::new(cert_file))
        .collect::<Result<_, _>>()
        .map_err(|e| anyhow::anyhow!("Failed to parse cert PEM: {e}"))?;

    let key_file = std::fs::File::open(key_path)
        .map_err(|e| anyhow::anyhow!("Cannot open key file {key_path}: {e}"))?;
    let key = private_key(&mut BufReader::new(key_file))
        .map_err(|e| anyhow::anyhow!("Failed to parse key PEM: {e}"))?
        .ok_or_else(|| anyhow::anyhow!("No private key found in {key_path}"))?;

    let tls_config = TlsServerConfig::builder()
        .with_no_client_auth()
        .with_single_cert(cert_chain, key)
        .map_err(|e| anyhow::anyhow!("Invalid TLS config: {e}"))?;

    Ok(TlsAcceptor::from(Arc::new(tls_config)))
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
