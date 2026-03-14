#include "opus_codec.h"

OpusCodec::OpusCodec() = default;

OpusCodec::~OpusCodec() {
    if (m_encoder) opus_encoder_destroy(m_encoder);
    if (m_decoder) opus_decoder_destroy(m_decoder);
}

bool OpusCodec::initialize(int bitrate, int complexity, bool enableFec, bool enableDtx) {
    int err;

    // Create encoder
    m_encoder = opus_encoder_create(SampleRate, Channels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !m_encoder) return false;

    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(complexity));
    opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(enableFec ? 1 : 0));
    opus_encoder_ctl(m_encoder, OPUS_SET_DTX(enableDtx ? 1 : 0));
    opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(5)); // expect ~5% loss

    // Create decoder
    m_decoder = opus_decoder_create(SampleRate, Channels, &err);
    if (err != OPUS_OK || !m_decoder) {
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
        return false;
    }

    return true;
}

std::vector<uint8_t> OpusCodec::encode(const float* pcm, int frameSize) {
    if (!m_encoder) return {};

    std::vector<uint8_t> packet(MaxPacketSize);
    int encoded = opus_encode_float(m_encoder, pcm, frameSize,
                                     packet.data(), MaxPacketSize);
    if (encoded < 0) return {};

    packet.resize(static_cast<size_t>(encoded));
    return packet;
}

int OpusCodec::decode(const uint8_t* packet, int packetLen, float* pcm, int maxFrameSize) {
    if (!m_decoder) return -1;

    int decoded = opus_decode_float(m_decoder, packet, packetLen, pcm, maxFrameSize, 0);
    return (decoded < 0) ? -1 : decoded;
}

int OpusCodec::decodePlc(float* pcm, int maxFrameSize) {
    if (!m_decoder) return -1;

    // Pass nullptr packet to trigger PLC
    int decoded = opus_decode_float(m_decoder, nullptr, 0, pcm, maxFrameSize, 0);
    return (decoded < 0) ? -1 : decoded;
}

void OpusCodec::setBitrate(int bitrate) {
    if (m_encoder) {
        opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(bitrate));
    }
}

void OpusCodec::setComplexity(int complexity) {
    if (m_encoder) {
        opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(complexity));
    }
}
