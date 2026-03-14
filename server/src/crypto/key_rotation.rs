use std::sync::Arc;
use std::time::Duration;

use dashmap::DashMap;
use std::sync::OnceLock;
use tracing::{debug, info};

use crate::crypto::key_exchange;
use crate::crypto::session_manager::SrtpSessionManager;
use crate::server::messages::{Envelope, KeyExchangeInitPayload};
use crate::state::connection_manager::ConnectionManager;

/// Ephemeral secrets for key rotation, keyed by user_id.
static ROTATION_SECRETS: OnceLock<DashMap<i32, x25519_dalek::EphemeralSecret>> = OnceLock::new();

fn secrets() -> &'static DashMap<i32, x25519_dalek::EphemeralSecret> {
    ROTATION_SECRETS.get_or_init(DashMap::new)
}

/// Take a rotation secret for a user (removes it from the store).
pub fn take_rotation_secret(user_id: i32) -> Option<x25519_dalek::EphemeralSecret> {
    secrets().remove(&user_id).map(|(_, s)| s)
}

/// Background task that initiates key rotation for all connected users
/// at a configurable interval (default: 30 minutes).
pub async fn key_rotation_task(
    interval_mins: u64,
    srtp_manager: Arc<SrtpSessionManager>,
    connection_manager: Arc<ConnectionManager>,
) {
    let interval = Duration::from_secs(interval_mins * 60);
    let mut timer = tokio::time::interval(interval);
    timer.tick().await; // Skip first immediate tick

    loop {
        timer.tick().await;

        let user_ids = connection_manager.all_user_ids();
        if user_ids.is_empty() {
            continue;
        }

        info!("Key rotation: initiating for {} connected users", user_ids.len());

        for user_id in user_ids {
            if !srtp_manager.has_session(user_id) {
                continue;
            }

            let (ke_secret, server_public) = key_exchange::generate_keypair();

            let public_key_b64 = {
                use base64::Engine;
                base64::engine::general_purpose::STANDARD.encode(&server_public)
            };

            let msg = Envelope::new("key_exchange_init", &KeyExchangeInitPayload {
                public_key: public_key_b64,
            });

            secrets().insert(user_id, ke_secret);
            connection_manager.send_to(user_id, &msg.to_json());
            debug!("Key rotation: sent new key exchange to user {}", user_id);
        }
    }
}
