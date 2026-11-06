// SPDX-License-Identifier: MPL-2.0
// The parts of crypto.h both backends share: the system's random numbers and identity files.
#include <cstdio>
#include <cstring>
#include <fstream>

#include "crypto.h"

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

}  // namespace online::proto::crypto
