use axum::{
    extract::State,
    http::header,
    response::IntoResponse,
    routing::get,
    Router,
};
use prometheus::{
    Encoder, IntGauge, IntCounter, Histogram, HistogramOpts, Opts, Registry, TextEncoder,
};
use std::sync::OnceLock;

use super::AppState;

/// Global metrics registry.
static METRICS: OnceLock<Metrics> = OnceLock::new();

pub struct Metrics {
    registry: Registry,
    pub active_connections: IntGauge,
    pub total_connections: IntCounter,
    pub total_messages_received: IntCounter,
    pub total_messages_sent: IntCounter,
    pub total_auth_successes: IntCounter,
    pub total_auth_failures: IntCounter,
    pub total_voice_packets: IntCounter,
    pub total_voice_packets_dropped: IntCounter,
    pub active_channels_with_users: IntGauge,
    pub ws_messages_rate_limited: IntCounter,
    pub admin_requests_rate_limited: IntCounter,
    pub message_handling_duration: Histogram,
}

impl Metrics {
    fn new() -> Self {
        let registry = Registry::new();

        let active_connections = IntGauge::with_opts(
            Opts::new("voip_active_connections", "Number of active WebSocket connections"),
        ).unwrap();
        let total_connections = IntCounter::with_opts(
            Opts::new("voip_total_connections", "Total WebSocket connections since start"),
        ).unwrap();
        let total_messages_received = IntCounter::with_opts(
            Opts::new("voip_messages_received_total", "Total WebSocket messages received"),
        ).unwrap();
        let total_messages_sent = IntCounter::with_opts(
            Opts::new("voip_messages_sent_total", "Total WebSocket messages sent"),
        ).unwrap();
        let total_auth_successes = IntCounter::with_opts(
            Opts::new("voip_auth_successes_total", "Total successful authentications"),
        ).unwrap();
        let total_auth_failures = IntCounter::with_opts(
            Opts::new("voip_auth_failures_total", "Total failed authentications"),
        ).unwrap();
        let total_voice_packets = IntCounter::with_opts(
            Opts::new("voip_voice_packets_total", "Total voice packets processed"),
        ).unwrap();
        let total_voice_packets_dropped = IntCounter::with_opts(
            Opts::new("voip_voice_packets_dropped_total", "Total voice packets dropped (rate limited)"),
        ).unwrap();
        let active_channels_with_users = IntGauge::with_opts(
            Opts::new("voip_active_channels", "Number of channels with at least one user"),
        ).unwrap();
        let ws_messages_rate_limited = IntCounter::with_opts(
            Opts::new("voip_ws_rate_limited_total", "Total WebSocket messages rate limited"),
        ).unwrap();
        let admin_requests_rate_limited = IntCounter::with_opts(
            Opts::new("voip_admin_rate_limited_total", "Total admin API requests rate limited"),
        ).unwrap();
        let message_handling_duration = Histogram::with_opts(
            HistogramOpts::new("voip_message_duration_seconds", "WebSocket message handling duration")
                .buckets(vec![0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0]),
        ).unwrap();

        registry.register(Box::new(active_connections.clone())).unwrap();
        registry.register(Box::new(total_connections.clone())).unwrap();
        registry.register(Box::new(total_messages_received.clone())).unwrap();
        registry.register(Box::new(total_messages_sent.clone())).unwrap();
        registry.register(Box::new(total_auth_successes.clone())).unwrap();
        registry.register(Box::new(total_auth_failures.clone())).unwrap();
        registry.register(Box::new(total_voice_packets.clone())).unwrap();
        registry.register(Box::new(total_voice_packets_dropped.clone())).unwrap();
        registry.register(Box::new(active_channels_with_users.clone())).unwrap();
        registry.register(Box::new(ws_messages_rate_limited.clone())).unwrap();
        registry.register(Box::new(admin_requests_rate_limited.clone())).unwrap();
        registry.register(Box::new(message_handling_duration.clone())).unwrap();

        Self {
            registry,
            active_connections,
            total_connections,
            total_messages_received,
            total_messages_sent,
            total_auth_successes,
            total_auth_failures,
            total_voice_packets,
            total_voice_packets_dropped,
            active_channels_with_users,
            ws_messages_rate_limited,
            admin_requests_rate_limited,
            message_handling_duration,
        }
    }
}

/// Get the global metrics instance.
pub fn metrics() -> &'static Metrics {
    METRICS.get_or_init(Metrics::new)
}

pub fn routes() -> Router<AppState> {
    Router::new().route("/metrics", get(metrics_handler))
}

async fn metrics_handler(State(state): State<AppState>) -> impl IntoResponse {
    let m = metrics();

    // Update gauges from live state
    m.active_connections.set(state.connection_manager.active_count() as i64);

    let encoder = TextEncoder::new();
    let mut buffer = Vec::new();
    if let Err(e) = encoder.encode(&m.registry.gather(), &mut buffer) {
        tracing::error!("Failed to encode metrics: {}", e);
        buffer = format!("# error encoding metrics: {}\n", e).into_bytes();
    }

    (
        [(header::CONTENT_TYPE, "text/plain; version=0.0.4; charset=utf-8")],
        buffer,
    )
}
