#include "Password.h"

#include <array>
#include <vector>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace {

std::string to_hex(const unsigned char* data, std::size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0f];
    }
    return out;
}

bool from_hex(const std::string& hex, std::array<unsigned char, 32>& out,
              std::size_t& out_size) {
    if (hex.size() % 2 != 0 || hex.size() / 2 > out.size())
        return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out_size = hex.size() / 2;
    for (std::size_t i = 0; i < out_size; ++i) {
        int hi = nibble(hex[i * 2]);
        int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

std::string hmac_sha256_hex(const unsigned char* key, std::size_t key_size,
                            const unsigned char* data, std::size_t data_size) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (!HMAC(EVP_sha256(), key, static_cast<int>(key_size), data,
              data_size, digest.data(), &digest_size))
        return {};
    return to_hex(digest.data(), digest_size);
}

}  // namespace

std::string random_hex(std::size_t bytes) {
    std::vector<unsigned char> buffer(bytes);
    if (RAND_bytes(buffer.data(), static_cast<int>(buffer.size())) != 1)
        return {};
    return to_hex(buffer.data(), buffer.size());
}

std::string hash_password(const std::string& password, const std::string& salt_hex) {
    std::array<unsigned char, 32> salt{};
    std::size_t salt_size = 0;
    if (!from_hex(salt_hex, salt, salt_size))
        return {};

    std::array<unsigned char, 32> digest{};
    constexpr int iterations = 120000;
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          salt.data(), static_cast<int>(salt_size), iterations,
                          EVP_sha256(), static_cast<int>(digest.size()),
                          digest.data()) != 1)
        return {};
    return to_hex(digest.data(), digest.size());
}

bool verify_password(const std::string& password, const std::string& salt_hex,
                     const std::string& expected_hash) {
    std::string actual = hash_password(password, salt_hex);
    if (actual.empty() || actual.size() != expected_hash.size())
        return false;
    return CRYPTO_memcmp(actual.data(), expected_hash.data(), actual.size()) == 0;
}

std::string make_stored_login_proof(const std::string& password_hash_hex,
                                    const std::string& nonce_hex) {
    std::array<unsigned char, 32> digest{};
    std::size_t digest_size = 0;
    std::array<unsigned char, 32> nonce{};
    std::size_t nonce_size = 0;
    if (!from_hex(password_hash_hex, digest, digest_size) ||
        !from_hex(nonce_hex, nonce, nonce_size) || digest_size == 0 ||
        nonce_size == 0)
        return {};
    return hmac_sha256_hex(digest.data(), digest_size, nonce.data(), nonce_size);
}

std::string make_login_proof(const std::string& password,
                             const std::string& salt_hex,
                             const std::string& nonce_hex) {
    const std::string password_hash = hash_password(password, salt_hex);
    if (password_hash.empty())
        return {};
    return make_stored_login_proof(password_hash, nonce_hex);
}

bool verify_login_proof(const std::string& proof_hex,
                        const std::string& password_hash_hex,
                        const std::string& nonce_hex) {
    std::string expected =
        make_stored_login_proof(password_hash_hex, nonce_hex);
    if (expected.empty() || expected.size() != proof_hex.size())
        return false;
    return CRYPTO_memcmp(expected.data(), proof_hex.data(), expected.size()) == 0;
}
