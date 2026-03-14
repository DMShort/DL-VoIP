#include "jitter_buffer.h"

JitterBuffer::JitterBuffer(int targetDelayMs)
    : m_targetDelayMs(targetDelayMs)
{
    // At 20ms per frame, target delay of 40ms = 2 frames of initial buffering
    m_initialBufferFrames = targetDelayMs / 20;
    if (m_initialBufferFrames < 1) m_initialBufferFrames = 1;
}

void JitterBuffer::push(uint64_t sequence, uint64_t timestamp, std::vector<uint8_t> payload) {
    // Drop very old packets
    if (m_started && sequence < m_nextSequence) {
        return;
    }

    // Prevent unbounded growth
    if (m_frames.size() >= MaxBufferedFrames) {
        // Drop oldest
        m_frames.erase(m_frames.begin());
    }

    Frame frame;
    frame.sequence = sequence;
    frame.timestamp = timestamp;
    frame.payload = std::move(payload);
    m_frames[sequence] = std::move(frame);
}

std::optional<JitterBuffer::Frame> JitterBuffer::pop() {
    if (!m_started) {
        // Wait until we've buffered enough frames
        if (static_cast<int>(m_frames.size()) < m_initialBufferFrames) {
            return std::nullopt;
        }
        m_started = true;
        m_nextSequence = m_frames.begin()->first;
    }

    // Check if the next expected frame is available
    auto it = m_frames.find(m_nextSequence);
    if (it != m_frames.end()) {
        Frame frame = std::move(it->second);
        m_frames.erase(it);
        m_nextSequence++;
        return frame;
    }

    // Missing packet — signal PLC with empty payload
    // But only if we have later frames (confirming it was lost, not just delayed)
    if (!m_frames.empty() && m_frames.begin()->first > m_nextSequence) {
        Frame plcFrame;
        plcFrame.sequence = m_nextSequence;
        plcFrame.timestamp = 0;
        // empty payload signals PLC
        m_nextSequence++;
        return plcFrame;
    }

    // No data at all — silence
    return std::nullopt;
}

void JitterBuffer::reset() {
    m_frames.clear();
    m_nextSequence = 0;
    m_started = false;
}
