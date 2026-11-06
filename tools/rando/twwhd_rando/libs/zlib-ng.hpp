#pragma once

// WWHD: the app's zlib instead of zlib-ng (only these calls are used, in wiiurpx.cpp).
// zlib-ng takes size_t lengths; zlib's uLongf is 32-bit on Windows (LLP64), so the two calls
// that pass a length pointer go through wrappers instead of being plain renames.
#include <cstddef>
#include <zlib.h>

inline int wwhd_zng_uncompress(Bytef* dest, size_t* destLen, const Bytef* source, size_t sourceLen) {
    uLongf n = static_cast<uLongf>(*destLen);
    const int r = uncompress(dest, &n, source, static_cast<uLong>(sourceLen));
    *destLen = n;
    return r;
}
inline int wwhd_zng_compress(Bytef* dest, size_t* destLen, const Bytef* source, size_t sourceLen) {
    uLongf n = static_cast<uLongf>(*destLen);
    const int r = compress(dest, &n, source, static_cast<uLong>(sourceLen));
    *destLen = n;
    return r;
}

#define zng_compress wwhd_zng_compress
#define zng_compressBound compressBound
#define zng_crc32 crc32
#define zng_uncompress wwhd_zng_uncompress
