#pragma once

#include <opus/opus.h>
#include <vector>
#include <cstdint>

/// Opus encoder/decoder wrapper for voice audio.
/// Configured for 48kHz mono, with FEC and DTX support.
class OpusCodec {
public:
    static constexpr int SampleRate = 48000;
    static constexpr int Channels = 1;
    static constexpr int FrameSize = 960;    // 20ms at 48kHz
    static constexpr int MaxPacketSize = 4000; // generous max for encoded frame

    OpusCodec();
    ~OpusCodec();

    OpusCodec(const OpusCodec&) = delete;
    OpusCodec& operator=(const OpusCodec&) = delete;

    /// Initialize encoder and decoder.
    /// Returns true on success.
    bool initialize(int bitrate = 32000, int complexity = 10,
                    bool enableFec = true, bool enableDtx = true);

    /// Encode one frame of float32 PCM audio.
    /// Input must be exactly FrameSize samples.
    /// Returns encoded Opus packet, or empty on failure.
    std::vector<uint8_t> encode(const float* pcm, int frameSize = FrameSize);

    /// Decode one Opus packet to float32 PCM audio.
    /// Output buffer must hold at least FrameSize samples.
    /// Returns number of decoded samples, or -1 on failure.
    int decode(const uint8_t* packet, int packetLen, float* pcm, int maxFrameSize = FrameSize);

    /// Decode with PLC (Packet Loss Concealment) — generates audio to fill gaps.
    /// Returns number of samples generated, or -1 on failure.
    int decodePlc(float* pcm, int maxFrameSize = FrameSize);

    /// Update encoder bitrate.
    void setBitrate(int bitrate);

    /// Update encoder complexity (0-10).
    void setComplexity(int complexity);

    bool isInitialized() const { return m_encoder != nullptr && m_decoder != nullptr; }

private:
    OpusEncoder* m_encoder = nullptr;
    OpusDecoder* m_decoder = nullptr;
};
