use aes_gcm::{
    aead::{Aead, KeyInit, generic_array::GenericArray},
    Aes128Gcm,
};

use super::key_exchange::DerivedKeys;

/// Per-user SRTP encryption/decryption session.
pub struct SrtpSession {
    cipher: Aes128Gcm,
    salt: [u8; 14],
}

impl SrtpSession {
    /// Create a new SRTP session from derived keys.
    pub fn new(keys: &DerivedKeys) -> Self {
        let cipher = Aes128Gcm::new_from_slice(&keys.master_key)
            .expect("Invalid AES key length");
        Self {
            cipher,
            salt: keys.master_salt,
        }
    }

    /// Encrypt a plaintext Opus payload.
    /// Returns ciphertext with appended 16-byte auth tag.
    pub fn encrypt(&self, plaintext: &[u8], sequence: u64) -> Result<Vec<u8>, SrtpError> {
        let nonce = self.derive_nonce(sequence);
        self.cipher
            .encrypt(&nonce, plaintext)
            .map_err(|_| SrtpError::EncryptionFailed)
    }

    /// Decrypt a ciphertext (with appended auth tag) to recover the Opus payload.
    /// Returns Err if authentication fails — caller MUST reject the packet.
    pub fn decrypt(&self, ciphertext: &[u8], sequence: u64) -> Result<Vec<u8>, SrtpError> {
        let nonce = self.derive_nonce(sequence);
        self.cipher
            .decrypt(&nonce, ciphertext)
            .map_err(|_| SrtpError::DecryptionFailed)
    }

    /// Derive a 12-byte nonce from the sequence number and salt.
    /// XOR the sequence number (zero-padded to 12 bytes) with the salt (zero-padded to 12 bytes).
    fn derive_nonce(&self, sequence: u64) -> GenericArray<u8, aes_gcm::aead::consts::U12> {
        let mut nonce_bytes = [0u8; 12];

        // Place sequence number in the last 8 bytes
        nonce_bytes[4..12].copy_from_slice(&sequence.to_be_bytes());

        // XOR with salt (first 12 bytes of the 14-byte salt)
        for i in 0..12 {
            if i < self.salt.len() {
                nonce_bytes[i] ^= self.salt[i];
            }
        }

        *GenericArray::from_slice(&nonce_bytes)
    }
}

#[derive(Debug, thiserror::Error)]
pub enum SrtpError {
    #[error("Encryption failed")]
    EncryptionFailed,
    #[error("Decryption failed — packet authentication failed")]
    DecryptionFailed,
}
