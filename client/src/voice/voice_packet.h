#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <optional>

/// Voice packet header format (28 bytes):
/// [Magic: 2 bytes "VL"] [Version: 1] [Flags: 1] [Sequence: 8] [Timestamp: 8]
/// [ChannelID: 4] [UserID: 4]
/// Followed by encrypted Opus payload.
struct VoicePacket {
    static constexpr uint8_t MAGIC[2] = {'V', 'L'};
    static constexpr uint8_t VERSION = 1;
    static constexpr size_t HEADER_SIZE = 28;

    uint8_t version = VERSION;
    uint8_t flags = 0;
    uint64_t sequence = 0;
    uint64_t timestamp = 0;
    uint32_t channelId = 0;
    uint32_t userId = 0;
    std::vector<uint8_t> payload; // encrypted Opus data

    /// Parse a voice packet from raw bytes. Returns nullopt if invalid.
    static std::optional<VoicePacket> parse(const uint8_t* data, size_t len);

    /// Serialize header + encrypted payload into bytes.
    std::vector<uint8_t> toBytes(const uint8_t* encryptedPayload, size_t payloadLen) const;

    /// Build a 28-byte header (for forwarding).
    std::array<uint8_t, HEADER_SIZE> buildHeader() const;

private:
    static void writeBE64(uint8_t* dst, uint64_t val);
    static void writeBE32(uint8_t* dst, uint32_t val);
    static uint64_t readBE64(const uint8_t* src);
    static uint32_t readBE32(const uint8_t* src);
};
