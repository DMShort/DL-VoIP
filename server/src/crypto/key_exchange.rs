use hkdf::Hkdf;
use rand::rngs::OsRng;
use sha2::Sha256;
use x25519_dalek::{EphemeralSecret, PublicKey, SharedSecret};

/// Derived SRTP keys from the shared secret.
pub struct DerivedKeys {
    pub master_key: [u8; 16],  // AES-128 key
    pub master_salt: [u8; 14], // SRTP salt
}

/// Generate an ephemeral X25519 keypair.
/// Returns (secret, public_key_bytes).
pub fn generate_keypair() -> (EphemeralSecret, [u8; 32]) {
    let secret = EphemeralSecret::random_from_rng(OsRng);
    let public = PublicKey::from(&secret);
    (secret, public.to_bytes())
}

/// Compute the shared secret from our secret key and the peer's public key.
pub fn compute_shared_secret(
    our_secret: EphemeralSecret,
    peer_public_bytes: &[u8; 32],
) -> SharedSecret {
    let peer_public = PublicKey::from(*peer_public_bytes);
    our_secret.diffie_hellman(&peer_public)
}

/// Derive SRTP master key and salt from the shared secret using HKDF-SHA256.
pub fn derive_keys(shared_secret: &[u8]) -> DerivedKeys {
    let hkdf = Hkdf::<Sha256>::new(Some(b"DadLink-SRTP-v2"), shared_secret);

    let mut master_key = [0u8; 16];
    hkdf.expand(b"srtp-master-key", &mut master_key)
        .expect("HKDF expand failed for master key");

    let mut master_salt = [0u8; 14];
    hkdf.expand(b"srtp-master-salt", &mut master_salt)
        .expect("HKDF expand failed for master salt");

    DerivedKeys {
        master_key,
        master_salt,
    }
}
