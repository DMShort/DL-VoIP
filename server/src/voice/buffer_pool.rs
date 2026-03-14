use std::sync::Arc;
use tokio::sync::Semaphore;
use crossbeam_queue::ArrayQueue;

/// Pre-allocated pool of byte buffers to avoid per-packet allocation.
pub struct BufferPool {
    pool: Arc<ArrayQueue<Vec<u8>>>,
    buffer_size: usize,
    _semaphore: Arc<Semaphore>,
}

impl BufferPool {
    pub fn new(pool_size: usize, buffer_size: usize) -> Self {
        let pool = Arc::new(ArrayQueue::new(pool_size));

        // Pre-allocate buffers
        for _ in 0..pool_size {
            let _ = pool.push(Vec::with_capacity(buffer_size));
        }

        Self {
            pool,
            buffer_size,
            _semaphore: Arc::new(Semaphore::new(pool_size)),
        }
    }

    /// Get a buffer from the pool, or allocate a new one if pool is empty.
    pub fn get(&self) -> Vec<u8> {
        self.pool.pop().unwrap_or_else(|| Vec::with_capacity(self.buffer_size))
    }

    /// Return a buffer to the pool for reuse.
    pub fn put(&self, mut buf: Vec<u8>) {
        buf.clear();
        let _ = self.pool.push(buf); // Drop if pool is full
    }
}
