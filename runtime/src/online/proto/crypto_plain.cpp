// SPDX-License-Identifier: MPL-2.0
// The "plain" crypto backend (crypto.h): the protocol's shapes without its security. INSECURE by design: public keys,
// signatures and session keys are all computable by an observer. Development and LAN tests only.
#include <cstdio>
#include <cstring>
#include <fstream>

#include "crypto.h"
#include "sha256.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#elif defined(__APPLE__)
#include <stdlib.h>
#else
#include <sys/random.h>
#endif

namespace online::proto::crypto {
namespace {

void derive(const char* label, const uint8_t* a, size_t an, const uint8_t* b, size_t bn, uint8_t out[32]) {
    Sha256 s;
    s.update(label, std::strlen(label));
    s.update(a, an);
    if (b) s.update(b, bn);
    s.final(out);
}

}  // namespace

const char* backend_name() { return "plain (INSECURE: no encryption, no authentication)"; }
bool backend_secure() { return false; }

void random_bytes(void* out, size_t n) {
#if defined(_WIN32)
    BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#elif defined(__APPLE__)
    arc4random_buf(out, n);
#else
    uint8_t* p = static_cast<uint8_t*>(out);
    while (n) {
        ssize_t got = getrandom(p, n, 0);
        if (got <= 0) continue;
        p += got;
        n -= size_t(got);
    }
#endif
}

Identity Identity::from_secret(const uint8_t secret[kKeyBytes]) {
    Identity id;
    std::memcpy(id.secret, secret, kKeyBytes);
    derive("wwhd-online plain pub", id.secret, kKeyBytes, nullptr, 0, id.pub);
    return id;
}

Identity Identity::generate() {
    uint8_t s[kKeyBytes];
    random_bytes(s, sizeof s);
    return from_secret(s);
}

bool Identity::save(const std::string& path, std::string& why) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        why = "cannot write " + path;
        return false;
    }
    for (uint8_t b : secret) std::fprintf(f, "%02x", b);
    std::fprintf(f, "\n");
    std::fclose(f);
    return true;
}

bool Identity::load_or_create(const std::string& path, Identity& out, std::string& why) {
    std::ifstream in(path);
    if (in) {
        std::string hex;
        in >> hex;
        if (hex.size() != 2 * kKeyBytes) {
            why = path + ": not an identity file";
            return false;
        }
        uint8_t s[kKeyBytes];
        for (size_t i = 0; i < kKeyBytes; ++i) {
            unsigned v = 0;
            if (std::sscanf(hex.c_str() + 2 * i, "%2x", &v) != 1) {
                why = path + ": not an identity file";
                return false;
            }
            s[i] = uint8_t(v);
        }
        out = from_secret(s);
        return true;
    }
    out = generate();
    return out.save(path, why);
}

void sign(const Identity& id, const void* msg, size_t n, uint8_t sig[kSigBytes]) {
    uint8_t k[32];
    derive("wwhd-online plain sig", id.pub, kKeyBytes, nullptr, 0, k);
    hmac_sha256(k, 32, msg, n, sig);
    hmac_sha256(sig, 32, msg, n, sig + 32);
}

bool verify(const uint8_t pub[kKeyBytes], const void* msg, size_t n, const uint8_t sig[kSigBytes]) {
    uint8_t k[32], want[kSigBytes];
    derive("wwhd-online plain sig", pub, kKeyBytes, nullptr, 0, k);
    hmac_sha256(k, 32, msg, n, want);
    hmac_sha256(want, 32, msg, n, want + 32);
    return std::memcmp(want, sig, kSigBytes) == 0;
}

Ephemeral Ephemeral::generate() {
    Ephemeral e;
    random_bytes(e.secret, kKeyBytes);
    derive("wwhd-online plain eph", e.secret, kKeyBytes, nullptr, 0, e.pub);
    return e;
}

bool session_keys(const Ephemeral& mine, const uint8_t their_pub[kKeyBytes], bool i_am_client, uint8_t tx[kKeyBytes],
                  uint8_t rx[kKeyBytes]) {
    const uint8_t* c = i_am_client ? mine.pub : their_pub;
    const uint8_t* s = i_am_client ? their_pub : mine.pub;
    uint8_t c2s[32], s2c[32];
    derive("wwhd-online plain c2s", c, kKeyBytes, s, kKeyBytes, c2s);
    derive("wwhd-online plain s2c", c, kKeyBytes, s, kKeyBytes, s2c);
    std::memcpy(tx, i_am_client ? c2s : s2c, kKeyBytes);
    std::memcpy(rx, i_am_client ? s2c : c2s, kKeyBytes);
    return true;
}

namespace {
void tag_of(const uint8_t key[kKeyBytes], uint64_t nonce, const void* ad, size_t ad_n, const uint8_t* data, size_t n,
            uint8_t out[32]) {
    uint8_t ipad[64] = {}, opad[64] = {};
    std::memcpy(ipad, key, kKeyBytes);
    std::memcpy(opad, key, kKeyBytes);
    for (int i = 0; i < 64; ++i) {
        ipad[i] ^= 0x36;
        opad[i] ^= 0x5c;
    }
    uint8_t nb[8];
    for (int i = 0; i < 8; ++i) nb[i] = uint8_t(nonce >> (8 * i));
    uint8_t inner[32];
    Sha256 a;
    a.update(ipad, 64);
    a.update(nb, 8);
    a.update(ad, ad_n);
    a.update(data, n);
    a.final(inner);
    Sha256 b;
    b.update(opad, 64);
    b.update(inner, 32);
    b.final(out);
}
}  // namespace

void seal(const uint8_t key[kKeyBytes], uint64_t nonce, const void* ad, size_t ad_n, uint8_t* data, size_t n,
          uint8_t tag[kTagBytes]) {
    uint8_t t[32];
    tag_of(key, nonce, ad, ad_n, data, n, t);
    std::memcpy(tag, t, kTagBytes);
}

bool open(const uint8_t key[kKeyBytes], uint64_t nonce, const void* ad, size_t ad_n, uint8_t* data, size_t n,
          const uint8_t tag[kTagBytes]) {
    uint8_t t[32];
    tag_of(key, nonce, ad, ad_n, data, n, t);
    uint8_t diff = 0;
    for (size_t i = 0; i < kTagBytes; ++i) diff |= uint8_t(t[i] ^ tag[i]);
    return diff == 0;
}

void hash(const void* data, size_t n, uint8_t out[32]) { sha256(data, n, out); }

}  // namespace online::proto::crypto
