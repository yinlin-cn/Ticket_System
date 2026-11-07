#pragma once

#include <cstddef>
#include <string>

std::string random_hex(std::size_t bytes);
std::string hash_password(const std::string& password, const std::string& salt_hex);
bool verify_password(const std::string& password, const std::string& salt_hex,
                     const std::string& expected_hash);
// Challenge-response login helpers. The HMAC key is the raw PBKDF2 digest,
// while the nonce is encoded as hex on the wire and decoded before HMAC.
std::string make_login_proof(const std::string& password,
                             const std::string& salt_hex,
                             const std::string& nonce_hex);
std::string make_stored_login_proof(const std::string& password_hash_hex,
                                    const std::string& nonce_hex);
bool verify_login_proof(const std::string& proof_hex,
                        const std::string& password_hash_hex,
                        const std::string& nonce_hex);
