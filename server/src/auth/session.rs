use std::time::{Duration, Instant};

use dashmap::DashMap;
use sqlx::PgPool;
use tracing::{debug, info};
use uuid::Uuid;

use crate::db;

/// In-memory session revocation cache with TTL.
/// Caches whether a session JTI is revoked to avoid DB hits on every message.
pub struct SessionCache {
    /// Maps JTI -> (is_revoked, cached_at)
    cache: DashMap<Uuid, (bool, Instant)>,
    ttl: Duration,
}

impl SessionCache {
    pub fn new(ttl_secs: u64) -> Self {
        Self {
            cache: DashMap::new(),
            ttl: Duration::from_secs(ttl_secs),
        }
    }

    /// Check if a session is valid (not revoked, not expired).
    /// Uses cache with TTL, falls back to DB on cache miss/expiry.
    pub async fn is_session_valid(&self, pool: &PgPool, jti: Uuid) -> bool {
        // Check cache first
        if let Some(entry) = self.cache.get(&jti) {
            let (is_revoked, cached_at) = *entry;
            if cached_at.elapsed() < self.ttl {
                return !is_revoked;
            }
        }

        // Cache miss or expired — check DB
        match db::sessions::is_revoked(pool, jti).await {
            Ok(is_revoked) => {
                self.cache.insert(jti, (is_revoked, Instant::now()));
                !is_revoked
            }
            Err(e) => {
                debug!("Failed to check session revocation: {}", e);
                // On DB error, deny access (fail closed)
                false
            }
        }
    }

    /// Immediately invalidate a session in the cache (called on local revocation).
    pub fn invalidate(&self, jti: Uuid) {
        self.cache.insert(jti, (true, Instant::now()));
    }

    /// Invalidate all sessions for a user (called when revoking all sessions).
    /// This clears the entire cache since we don't index by user_id.
    /// The cache will repopulate on next access.
    pub fn invalidate_all(&self) {
        self.cache.clear();
    }

    /// Remove stale entries from the cache. Called periodically.
    pub fn cleanup(&self) {
        let before = self.cache.len();
        self.cache.retain(|_, (_, cached_at)| cached_at.elapsed() < self.ttl * 10);
        let removed = before - self.cache.len();
        if removed > 0 {
            debug!("Session cache cleanup: removed {} stale entries", removed);
        }
    }
}

/// Background task: clean up expired sessions from the DB every 10 minutes.
pub async fn session_cleanup_task(pool: PgPool) {
    let mut interval = tokio::time::interval(Duration::from_secs(600));
    loop {
        interval.tick().await;
        match db::sessions::cleanup_expired(&pool).await {
            Ok(count) => {
                if count > 0 {
                    info!("Cleaned up {} expired sessions", count);
                }
            }
            Err(e) => {
                debug!("Session cleanup error: {}", e);
            }
        }
    }
}
