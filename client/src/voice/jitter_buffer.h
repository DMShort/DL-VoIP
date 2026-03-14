#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <optional>

/// Per-source jitter buffer for voice packets.
/// Sequence-ordered, adaptive 20-40ms buffering, signals PLC on gaps.
class JitterBuffer {
public:
    struct Frame {
        uint64_t sequence;
        uint64_t timestamp;
        std::vector<uint8_t> payload; // decrypted Opus data
    };

    explicit JitterBuffer(int targetDelayMs = 40);

    /// Insert a received frame into the buffer.
    void push(uint64_t sequence, uint64_t timestamp, std::vector<uint8_t> payload);

    /// Pop the next frame in sequence order.
    /// Returns nullopt if we should wait (buffer building up).
    /// Returns a frame with empty payload to signal PLC (missing packet).
    std::optional<Frame> pop();

    /// Reset the buffer (e.g., on channel switch).
    void reset();

    /// Number of frames currently buffered.
    size_t size() const { return m_frames.size(); }

private:
    std::map<uint64_t, Frame> m_frames; // sequence -> frame
    uint64_t m_nextSequence = 0;
    bool m_started = false;
    int m_targetDelayMs;
    int m_initialBufferFrames; // frames to buffer before starting playback

    static constexpr size_t MaxBufferedFrames = 50; // ~1 second at 20ms frames
};
