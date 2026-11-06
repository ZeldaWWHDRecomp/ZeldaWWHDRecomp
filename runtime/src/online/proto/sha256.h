// SPDX-License-Identifier: MPL-2.0
// SHA-256 (FIPS 180-4) and HMAC-SHA-256 (RFC 2104): a small dependency-free implementation for the online
// protocol's hashes (friend codes, proof-of-work, challenge cookies, the plain crypto backend).
#pragma once
#include <cstddef>
#include <cstdint>

namespace online::proto {

struct Sha256 {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t len = 0;
    Sha256();
    void update(const void* data, size_t n);
    void final(uint8_t out[32]);
};

void sha256(const void* data, size_t n, uint8_t out[32]);
void hmac_sha256(const uint8_t* key, size_t key_n, const void* data, size_t n, uint8_t out[32]);

}  // namespace online::proto
