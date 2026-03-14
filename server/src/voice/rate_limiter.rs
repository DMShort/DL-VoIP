use std::net::SocketAddr;
use std::time::Instant;

use dashmap::DashMap;

/// Per-source-address token bucket rate limiter for UDP voice packets.
pub struct VoiceRateLimiter {
    buckets: DashMap<SocketAddr, TokenBucket>,
    max_rate: u32, // packets per second
}

struct TokenBucket {
    tokens: f64,
    last_refill: Instant,
}

impl VoiceRateLimiter {
    pub fn new(max_packets_per_sec: u32) -> Self {
        Self {
            buckets: DashMap::new(),
            max_rate: max_packets_per_sec,
        }
    }

    /// Check if a packet from the given address should be allowed.
    /// Returns true if allowed, false if rate-limited (drop silently).
    pub fn allow(&self, addr: SocketAddr) -> bool {
        let mut entry = self.buckets.entry(addr).or_insert_with(|| TokenBucket {
            tokens: self.max_rate as f64,
            last_refill: Instant::now(),
        });

        let bucket = entry.value_mut();
        let now = Instant::now();
        let elapsed = now.duration_since(bucket.last_refill).as_secs_f64();

        // Refill tokens
        bucket.tokens = (bucket.tokens + elapsed * self.max_rate as f64).min(self.max_rate as f64);
        bucket.last_refill = now;

        if bucket.tokens >= 1.0 {
            bucket.tokens -= 1.0;
            true
        } else {
            false
        }
    }

    /// Periodic cleanup of stale entries (call every ~60 seconds).
    pub fn cleanup(&self) {
        let cutoff = Instant::now() - std::time::Duration::from_secs(60);
        self.buckets.retain(|_, v| v.last_refill > cutoff);
    }
}
