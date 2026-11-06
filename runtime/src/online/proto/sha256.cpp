// SPDX-License-Identifier: MPL-2.0
// SHA-256 written from FIPS 180-4; the round constants are the fractional parts of the cube roots of the first 64
// primes and are computed at first use (no hand-typed table). proto_test checks the FIPS/RFC test vectors.
#include "sha256.h"

#include <cmath>
#include <cstring>

namespace online::proto {
namespace {

struct Tables {
    uint32_t k[64];
    uint32_t h0[8];
    Tables() {
        int found = 0;
        for (int n = 2; found < 64; ++n) {
            bool prime = true;
            for (int d = 2; d * d <= n; ++d)
                if (n % d == 0) {
                    prime = false;
                    break;
                }
            if (!prime) continue;
            long double c = std::cbrt(static_cast<long double>(n));
            k[found] = static_cast<uint32_t>((c - std::floor(c)) * 4294967296.0L);
            if (found < 8) {
                long double s = std::sqrt(static_cast<long double>(n));
                h0[found] = static_cast<uint32_t>((s - std::floor(s)) * 4294967296.0L);
            }
            ++found;
        }
    }
};
const Tables& tables() {
    static const Tables t;
    return t;
}

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void block(uint32_t h[8], const uint8_t* p) {
    const uint32_t* k = tables().k;
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) | (uint32_t(p[4 * i + 2]) << 8) |
               uint32_t(p[4 * i + 3]);
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + k[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

}  // namespace

Sha256::Sha256() { std::memcpy(h, tables().h0, sizeof h); }

void Sha256::update(const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t have = len % 64;
    len += n;
    if (have) {
        size_t take = 64 - have < n ? 64 - have : n;
        std::memcpy(buf + have, p, take);
        p += take;
        n -= take;
        if (have + take < 64) return;
        block(h, buf);
    }
    while (n >= 64) {
        block(h, p);
        p += 64;
        n -= 64;
    }
    if (n) std::memcpy(buf, p, n);
}

void Sha256::final(uint8_t out[32]) {
    uint64_t bits = len * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (len % 64 != 56) update(&zero, 1);
    uint8_t be[8];
    for (int i = 0; i < 8; ++i) be[i] = uint8_t(bits >> (56 - 8 * i));
    update(be, 8);
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = uint8_t(h[i] >> 24);
        out[4 * i + 1] = uint8_t(h[i] >> 16);
        out[4 * i + 2] = uint8_t(h[i] >> 8);
        out[4 * i + 3] = uint8_t(h[i]);
    }
}

void sha256(const void* data, size_t n, uint8_t out[32]) {
    Sha256 s;
    s.update(data, n);
    s.final(out);
}

void hmac_sha256(const uint8_t* key, size_t key_n, const void* data, size_t n, uint8_t out[32]) {
    uint8_t k[64] = {};
    if (key_n > 64)
        sha256(key, key_n, k);
    else
        std::memcpy(k, key, key_n);
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    uint8_t inner[32];
    Sha256 a;
    a.update(ipad, 64);
    a.update(data, n);
    a.final(inner);
    Sha256 b;
    b.update(opad, 64);
    b.update(inner, 32);
    b.final(out);
}

}  // namespace online::proto
