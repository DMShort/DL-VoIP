#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>

/// Derived SRTP keys — matches server's DerivedKeys structure.
struct DerivedKeys {
    std::array<uint8_t, 16> masterKey;   // AES-128 key
    std::array<uint8_t, 14> masterSalt;  // SRTP salt
};

/// X25519 key exchange using OpenSSL EVP API.
/// Generates ephemeral keypairs, computes shared secrets, derives SRTP keys.
class KeyExchange {
public:
    KeyExchange();
    ~KeyExchange();

    KeyExchange(const KeyExchange&) = delete;
    KeyExchange& operator=(const KeyExchange&) = delete;

    /// Generate a new ephemeral X25519 keypair.
    /// Returns true on success.
    bool generateKeypair();

    /// Get our public key as raw 32 bytes.
    std::array<uint8_t, 32> publicKeyBytes() const;

    /// Get our public key as a base64-encoded string.
    std::string publicKeyBase64() const;

    /// Compute the shared secret from our private key and the peer's public key (base64).
    /// Returns true on success. After this call, derivedKeys() is valid.
    bool computeSharedSecret(const std::string& peerPublicKeyBase64);

    /// Compute the shared secret from our private key and the peer's raw public key bytes.
    bool computeSharedSecret(const std::array<uint8_t, 32>& peerPublicKey);

    /// Get the derived SRTP keys (valid after computeSharedSecret succeeds).
    const DerivedKeys& derivedKeys() const { return m_derivedKeys; }

    /// Whether keys have been derived successfully.
    bool hasKeys() const { return m_hasKeys; }

private:
    bool deriveKeys(const std::vector<uint8_t>& sharedSecret);

    struct Impl;
    Impl* m_impl;
    DerivedKeys m_derivedKeys{};
    bool m_hasKeys = false;
};
