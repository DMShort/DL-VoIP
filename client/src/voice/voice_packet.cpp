#include "voice_packet.h"

#include <cstring>

std::optional<VoicePacket> VoicePacket::parse(const uint8_t* data, size_t len) {
    if (len < HEADER_SIZE) return std::nullopt;

    // Validate magic
    if (data[0] != MAGIC[0] || data[1] != MAGIC[1]) return std::nullopt;

    VoicePacket pkt;
    pkt.version = data[2];
    pkt.flags = data[3];
    pkt.sequence = readBE64(data + 4);
    pkt.timestamp = readBE64(data + 12);
    pkt.channelId = readBE32(data + 20);
    pkt.userId = readBE32(data + 24);

    if (len > HEADER_SIZE) {
        pkt.payload.assign(data + HEADER_SIZE, data + len);
    }

    return pkt;
}

std::vector<uint8_t> VoicePacket::toBytes(const uint8_t* encryptedPayload, size_t payloadLen) const {
    std::vector<uint8_t> buf;
    buf.reserve(HEADER_SIZE + payloadLen);

    auto header = buildHeader();
    buf.insert(buf.end(), header.begin(), header.end());
    buf.insert(buf.end(), encryptedPayload, encryptedPayload + payloadLen);

    return buf;
}

std::array<uint8_t, VoicePacket::HEADER_SIZE> VoicePacket::buildHeader() const {
    std::array<uint8_t, HEADER_SIZE> header{};
    header[0] = MAGIC[0];
    header[1] = MAGIC[1];
    header[2] = version;
    header[3] = flags;
    writeBE64(header.data() + 4, sequence);
    writeBE64(header.data() + 12, timestamp);
    writeBE32(header.data() + 20, channelId);
    writeBE32(header.data() + 24, userId);
    return header;
}

void VoicePacket::writeBE64(uint8_t* dst, uint64_t val) {
    dst[0] = static_cast<uint8_t>((val >> 56) & 0xFF);
    dst[1] = static_cast<uint8_t>((val >> 48) & 0xFF);
    dst[2] = static_cast<uint8_t>((val >> 40) & 0xFF);
    dst[3] = static_cast<uint8_t>((val >> 32) & 0xFF);
    dst[4] = static_cast<uint8_t>((val >> 24) & 0xFF);
    dst[5] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dst[6] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[7] = static_cast<uint8_t>(val & 0xFF);
}

void VoicePacket::writeBE32(uint8_t* dst, uint32_t val) {
    dst[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(val & 0xFF);
}

uint64_t VoicePacket::readBE64(const uint8_t* src) {
    return (static_cast<uint64_t>(src[0]) << 56)
         | (static_cast<uint64_t>(src[1]) << 48)
         | (static_cast<uint64_t>(src[2]) << 40)
         | (static_cast<uint64_t>(src[3]) << 32)
         | (static_cast<uint64_t>(src[4]) << 24)
         | (static_cast<uint64_t>(src[5]) << 16)
         | (static_cast<uint64_t>(src[6]) << 8)
         | static_cast<uint64_t>(src[7]);
}

uint32_t VoicePacket::readBE32(const uint8_t* src) {
    return (static_cast<uint32_t>(src[0]) << 24)
         | (static_cast<uint32_t>(src[1]) << 16)
         | (static_cast<uint32_t>(src[2]) << 8)
         | static_cast<uint32_t>(src[3]);
}
