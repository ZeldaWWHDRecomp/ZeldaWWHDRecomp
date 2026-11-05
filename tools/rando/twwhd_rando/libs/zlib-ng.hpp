#pragma once

// WWHD: the app's zlib instead of zlib-ng (only these calls are used, in wiiurpx.cpp)
#include <zlib.h>
#define zng_compress compress
#define zng_compressBound compressBound
#define zng_crc32 crc32
#define zng_uncompress uncompress
