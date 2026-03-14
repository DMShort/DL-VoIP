#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

/// Lock-free single-producer single-consumer (SPSC) ring buffer.
/// Safe for use in audio callbacks — no allocations, no mutexes, no syscalls.
/// Size must be a power of 2.
template <typename T>
class RingBuffer {
public:
    /// Construct with the given capacity (rounded up to next power of 2).
    explicit RingBuffer(size_t capacity) {
        // Round up to next power of 2
        m_capacity = 1;
        while (m_capacity < capacity) {
            m_capacity <<= 1;
        }
        m_mask = m_capacity - 1;
        m_buffer.resize(m_capacity);
        m_readIdx.store(0, std::memory_order_relaxed);
        m_writeIdx.store(0, std::memory_order_relaxed);
    }

    /// Number of items available to read.
    size_t availableRead() const {
        size_t w = m_writeIdx.load(std::memory_order_acquire);
        size_t r = m_readIdx.load(std::memory_order_relaxed);
        return w - r;
    }

    /// Number of slots available to write.
    size_t availableWrite() const {
        size_t r = m_readIdx.load(std::memory_order_acquire);
        size_t w = m_writeIdx.load(std::memory_order_relaxed);
        return m_capacity - (w - r);
    }

    /// Write up to `count` items from `src`. Returns number actually written.
    /// Producer thread only.
    size_t write(const T* src, size_t count) {
        size_t avail = availableWrite();
        if (count > avail) count = avail;

        size_t w = m_writeIdx.load(std::memory_order_relaxed);
        size_t pos = w & m_mask;

        // Handle wrap-around with two copies
        size_t firstChunk = m_capacity - pos;
        if (firstChunk > count) firstChunk = count;

        std::memcpy(&m_buffer[pos], src, firstChunk * sizeof(T));
        if (count > firstChunk) {
            std::memcpy(&m_buffer[0], src + firstChunk, (count - firstChunk) * sizeof(T));
        }

        m_writeIdx.store(w + count, std::memory_order_release);
        return count;
    }

    /// Read up to `count` items into `dst`. Returns number actually read.
    /// Consumer thread only.
    size_t read(T* dst, size_t count) {
        size_t avail = availableRead();
        if (count > avail) count = avail;

        size_t r = m_readIdx.load(std::memory_order_relaxed);
        size_t pos = r & m_mask;

        size_t firstChunk = m_capacity - pos;
        if (firstChunk > count) firstChunk = count;

        std::memcpy(dst, &m_buffer[pos], firstChunk * sizeof(T));
        if (count > firstChunk) {
            std::memcpy(dst + firstChunk, &m_buffer[0], (count - firstChunk) * sizeof(T));
        }

        m_readIdx.store(r + count, std::memory_order_release);
        return count;
    }

    /// Reset the buffer (not thread-safe — call only when both threads are idle).
    void reset() {
        m_readIdx.store(0, std::memory_order_relaxed);
        m_writeIdx.store(0, std::memory_order_relaxed);
    }

    size_t capacity() const { return m_capacity; }

private:
    std::vector<T> m_buffer;
    size_t m_capacity;
    size_t m_mask;
    alignas(64) std::atomic<size_t> m_readIdx;
    alignas(64) std::atomic<size_t> m_writeIdx;
};
