// Splaton't engine — SHA-256 for salted password hashing (local accounts)
#pragma once
#include <cstddef>
#include <string>

std::string sha256_hex(const void* data, size_t len);
inline std::string sha256_hex(const std::string& s) { return sha256_hex(s.data(), s.size()); }
std::string random_hex(size_t bytes); // cryptographic-ish salt from std::random_device
