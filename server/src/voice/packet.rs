/// Voice packet header format (28 bytes):
/// [Magic: 2 bytes "VL"] [Version: 1] [Flags: 1] [Sequence: 8] [Timestamp: 8] [ChannelID: 4] [UserID: 4]
/// Followed by encrypted Opus payload.

pub const MAGIC: [u8; 2] = [b'V', b'L'];
pub const VERSION: u8 = 1;
pub const HEADER_SIZE: usize = 28;

#[derive(Debug, Clone)]
pub struct VoicePacket {
    pub version: u8,
    pub flags: u8,
    pub sequence: u64,
    pub timestamp: u64,
    pub channel_id: u32,
    pub user_id: u32,
    pub payload: Vec<u8>, // encrypted Opus data
}

impl VoicePacket {
    /// Parse a voice packet from raw bytes. Returns None if invalid.
    pub fn parse(data: &[u8]) -> Option<Self> {
        if data.len() < HEADER_SIZE {
            return None;
        }

        // Validate magic number
        if data[0] != MAGIC[0] || data[1] != MAGIC[1] {
            return None;
        }

        let version = data[2];
        let flags = data[3];
        let sequence = u64::from_be_bytes(data[4..12].try_into().ok()?);
        let timestamp = u64::from_be_bytes(data[12..20].try_into().ok()?);
        let channel_id = u32::from_be_bytes(data[20..24].try_into().ok()?);
        let user_id = u32::from_be_bytes(data[24..28].try_into().ok()?);
        let payload = data[HEADER_SIZE..].to_vec();

        Some(VoicePacket {
            version,
            flags,
            sequence,
            timestamp,
            channel_id,
            user_id,
            payload,
        })
    }

    /// Serialize the header + payload into bytes.
    pub fn to_bytes(&self, encrypted_payload: &[u8]) -> Vec<u8> {
        let mut buf = Vec::with_capacity(HEADER_SIZE + encrypted_payload.len());
        buf.extend_from_slice(&MAGIC);
        buf.push(self.version);
        buf.push(self.flags);
        buf.extend_from_slice(&self.sequence.to_be_bytes());
        buf.extend_from_slice(&self.timestamp.to_be_bytes());
        buf.extend_from_slice(&self.channel_id.to_be_bytes());
        buf.extend_from_slice(&self.user_id.to_be_bytes());
        buf.extend_from_slice(encrypted_payload);
        buf
    }

    /// Build a header for forwarding (rewrite user_id, keep everything else).
    pub fn build_forward_header(&self) -> [u8; HEADER_SIZE] {
        let mut header = [0u8; HEADER_SIZE];
        header[0..2].copy_from_slice(&MAGIC);
        header[2] = self.version;
        header[3] = self.flags;
        header[4..12].copy_from_slice(&self.sequence.to_be_bytes());
        header[12..20].copy_from_slice(&self.timestamp.to_be_bytes());
        header[20..24].copy_from_slice(&self.channel_id.to_be_bytes());
        header[24..28].copy_from_slice(&self.user_id.to_be_bytes());
        header
    }
}
