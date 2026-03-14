#include "key_exchange.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <cstring>
#include <stdexcept>

// Base64 encode/decode helpers
static std::string base64Encode(const uint8_t* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);

    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

static std::vector<uint8_t> base64Decode(const std::string& encoded) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    std::vector<uint8_t> result(encoded.size());
    int decoded_len = BIO_read(b64, result.data(), static_cast<int>(result.size()));
    BIO_free_all(b64);

    if (decoded_len < 0) return {};
    result.resize(static_cast<size_t>(decoded_len));
    return result;
}

struct KeyExchange::Impl {
    EVP_PKEY* pkey = nullptr;

    ~Impl() {
        if (pkey) EVP_PKEY_free(pkey);
    }
};

KeyExchange::KeyExchange()
    : m_impl(new Impl)
{}

KeyExchange::~KeyExchange() {
    delete m_impl;
}

bool KeyExchange::generateKeypair() {
    if (m_impl->pkey) {
        EVP_PKEY_free(m_impl->pkey);
        m_impl->pkey = nullptr;
    }

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!ctx) return false;

    bool ok = false;
    if (EVP_PKEY_keygen_init(ctx) == 1) {
        if (EVP_PKEY_keygen(ctx, &m_impl->pkey) == 1) {
            ok = true;
        }
    }

    EVP_PKEY_CTX_free(ctx);
    m_hasKeys = false;
    return ok;
}

std::array<uint8_t, 32> KeyExchange::publicKeyBytes() const {
    std::array<uint8_t, 32> pubkey{};
    if (!m_impl->pkey) return pubkey;

    size_t len = 32;
    EVP_PKEY_get_raw_public_key(m_impl->pkey, pubkey.data(), &len);
    return pubkey;
}

std::string KeyExchange::publicKeyBase64() const {
    auto pubkey = publicKeyBytes();
    return base64Encode(pubkey.data(), pubkey.size());
}

bool KeyExchange::computeSharedSecret(const std::string& peerPublicKeyBase64) {
    auto peerBytes = base64Decode(peerPublicKeyBase64);
    if (peerBytes.size() != 32) return false;

    std::array<uint8_t, 32> peerKey;
    std::memcpy(peerKey.data(), peerBytes.data(), 32);
    return computeSharedSecret(peerKey);
}

bool KeyExchange::computeSharedSecret(const std::array<uint8_t, 32>& peerPublicKey) {
    if (!m_impl->pkey) return false;

    // Create peer EVP_PKEY from raw bytes
    EVP_PKEY* peerPkey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519, nullptr, peerPublicKey.data(), 32);
    if (!peerPkey) return false;

    // Derive shared secret
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(m_impl->pkey, nullptr);
    if (!ctx) {
        EVP_PKEY_free(peerPkey);
        return false;
    }

    bool ok = false;
    if (EVP_PKEY_derive_init(ctx) == 1) {
        if (EVP_PKEY_derive_set_peer(ctx, peerPkey) == 1) {
            size_t secretLen = 0;
            if (EVP_PKEY_derive(ctx, nullptr, &secretLen) == 1) {
                std::vector<uint8_t> secret(secretLen);
                if (EVP_PKEY_derive(ctx, secret.data(), &secretLen) == 1) {
                    secret.resize(secretLen);
                    ok = deriveKeys(secret);
                }
            }
        }
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peerPkey);
    return ok;
}

bool KeyExchange::deriveKeys(const std::vector<uint8_t>& sharedSecret) {
    // HKDF-SHA256 matching the server:
    //   salt: b"DadLink-SRTP-v2"
    //   info for master key: b"srtp-master-key"
    //   info for master salt: b"srtp-master-salt"

    const uint8_t hkdfSalt[] = "DadLink-SRTP-v2";
    size_t hkdfSaltLen = sizeof(hkdfSalt) - 1; // exclude null terminator

    // Derive master key (16 bytes)
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!ctx) return false;

        const uint8_t info[] = "srtp-master-key";
        size_t infoLen = sizeof(info) - 1;

        uint8_t key[16];
        size_t keyLen = 16;

        bool ok = EVP_PKEY_derive_init(ctx) == 1
            && EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) == 1
            && EVP_PKEY_CTX_set1_hkdf_salt(ctx, hkdfSalt, static_cast<int>(hkdfSaltLen)) == 1
            && EVP_PKEY_CTX_set1_hkdf_key(ctx, sharedSecret.data(), static_cast<int>(sharedSecret.size())) == 1
            && EVP_PKEY_CTX_add1_hkdf_info(ctx, info, static_cast<int>(infoLen)) == 1
            && EVP_PKEY_derive(ctx, key, &keyLen) == 1;

        EVP_PKEY_CTX_free(ctx);
        if (!ok) return false;

        std::memcpy(m_derivedKeys.masterKey.data(), key, 16);
    }

    // Derive master salt (14 bytes)
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!ctx) return false;

        const uint8_t info[] = "srtp-master-salt";
        size_t infoLen = sizeof(info) - 1;

        uint8_t salt[14];
        size_t saltLen = 14;

        bool ok = EVP_PKEY_derive_init(ctx) == 1
            && EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) == 1
            && EVP_PKEY_CTX_set1_hkdf_salt(ctx, hkdfSalt, static_cast<int>(hkdfSaltLen)) == 1
            && EVP_PKEY_CTX_set1_hkdf_key(ctx, sharedSecret.data(), static_cast<int>(sharedSecret.size())) == 1
            && EVP_PKEY_CTX_add1_hkdf_info(ctx, info, static_cast<int>(infoLen)) == 1
            && EVP_PKEY_derive(ctx, salt, &saltLen) == 1;

        EVP_PKEY_CTX_free(ctx);
        if (!ok) return false;

        std::memcpy(m_derivedKeys.masterSalt.data(), salt, 14);
    }

    m_hasKeys = true;
    return true;
}
