use std::sync::OnceLock;
use std::time::{Duration, Instant};

use dashmap::DashMap;

/// Global auth rate limiter.
static AUTH_LIMITER: OnceLock<AuthRateLimiter> = OnceLock::new();

/// Global admin API rate limiter.
static ADMIN_LIMITER: OnceLock<AdminRateLimiter> = OnceLock::new();

pub fn auth_limiter() -> &'static AuthRateLimiter {
    AUTH_LIMITER.get_or_init(AuthRateLimiter::new)
}

pub fn admin_limiter() -> &'static AdminRateLimiter {
    ADMIN_LIMITER.get_or_init(AdminRateLimiter::new)
}

// ---------------------------------------------------------------------------
// Auth rate limiter: sliding-window failure counter
// ---------------------------------------------------------------------------

/// Tracks auth failures per IP (5/min) and per username (10/min).
pub struct AuthRateLimiter {
    ip_failures: DashMap<String, Vec<Instant>>,
    username_failures: DashMap<String, Vec<Instant>>,
    ip_limit: u32,
    username_limit: u32,
    window: Duration,
}

impl AuthRateLimiter {
    fn new() -> Self {
        Self {
            ip_failures: DashMap::new(),
            username_failures: DashMap::new(),
            ip_limit: 5,
            username_limit: 10,
            window: Duration::from_secs(60),
        }
    }

    /// Check if an auth attempt from this IP is allowed.
    pub fn check_ip(&self, ip: &str) -> bool {
        self.check(&self.ip_failures, ip, self.ip_limit)
    }

    /// Check if an auth attempt for this username is allowed.
    pub fn check_username(&self, username: &str) -> bool {
        self.check(&self.username_failures, username, self.username_limit)
    }

    /// Record a failed auth attempt for the given IP.
    pub fn record_ip_failure(&self, ip: &str) {
        self.record(&self.ip_failures, ip);
    }

    /// Record a failed auth attempt for the given username.
    pub fn record_username_failure(&self, username: &str) {
        self.record(&self.username_failures, username);
    }

    fn check(&self, map: &DashMap<String, Vec<Instant>>, key: &str, limit: u32) -> bool {
        let now = Instant::now();
        if let Some(mut entry) = map.get_mut(key) {
            entry.retain(|t| now.duration_since(*t) < self.window);
            entry.len() < limit as usize
        } else {
            true
        }
    }

    fn record(&self, map: &DashMap<String, Vec<Instant>>, key: &str) {
        map.entry(key.to_string())
            .or_default()
            .push(Instant::now());
    }

    /// Periodic cleanup of expired entries to prevent memory growth.
    pub fn cleanup(&self) {
        let now = Instant::now();
        self.ip_failures.retain(|_, v| {
            v.retain(|t| now.duration_since(*t) < self.window);
            !v.is_empty()
        });
        self.username_failures.retain(|_, v| {
            v.retain(|t| now.duration_since(*t) < self.window);
            !v.is_empty()
        });
    }
}

// ---------------------------------------------------------------------------
// Admin API rate limiter: token bucket per user (60 req/min)
// ---------------------------------------------------------------------------

struct AdminBucket {
    tokens: f64,
    last_refill: Instant,
}

pub struct AdminRateLimiter {
    buckets: DashMap<i32, AdminBucket>,
    rate: f64,     // tokens per second
    capacity: f64, // max burst
}

impl AdminRateLimiter {
    fn new() -> Self {
        Self {
            buckets: DashMap::new(),
            rate: 1.0,      // 60 per minute = 1 per second
            capacity: 10.0,  // allow small burst
        }
    }

    /// Returns true if the request is allowed.
    pub fn check_and_consume(&self, user_id: i32) -> bool {
        let now = Instant::now();
        let mut entry = self.buckets.entry(user_id).or_insert_with(|| AdminBucket {
            tokens: self.capacity,
            last_refill: now,
        });

        let elapsed = now.duration_since(entry.last_refill).as_secs_f64();
        entry.tokens = (entry.tokens + elapsed * self.rate).min(self.capacity);
        entry.last_refill = now;

        if entry.tokens >= 1.0 {
            entry.tokens -= 1.0;
            true
        } else {
            false
        }
    }

    /// Periodic cleanup of idle buckets.
    pub fn cleanup(&self) {
        let now = Instant::now();
        let idle_threshold = Duration::from_secs(300); // 5 minutes
        self.buckets
            .retain(|_, v| now.duration_since(v.last_refill) < idle_threshold);
    }
}
