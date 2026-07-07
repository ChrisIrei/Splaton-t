// Splaton't engine — SHA-256 for salted password hashing (local accounts)
#pragma once
#include <cstddef>
#include <string>

void sha256_raw(const void* data, size_t len, unsigned char out[32]);
std::string sha256_hex(const void* data, size_t len);
inline std::string sha256_hex(const std::string& s) { return sha256_hex(s.data(), s.size()); }
void hmac_sha256(const void* key, size_t keyLen, const void* msg, size_t msgLen, unsigned char out[32]);
// PBKDF2-HMAC-SHA256, 32-byte key, hex output. saltHex is hex-decoded first.
std::string pbkdf2_hex(const std::string& password, const std::string& saltHex, int iterations);
std::string random_hex(size_t bytes); // cryptographic-ish salt from std::random_device
