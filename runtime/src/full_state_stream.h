// Bounded full-state compression. v1 Apple framing and v2 raw LZ4 remain readable;
// v3 uses independently checksummed 64KiB zstd frames on every platform.
#pragma once
#include "full_state_header.h"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <lz4.h>
#include <zstd.h>
#ifdef __APPLE__
#include <compression.h>
#endif
namespace ss {
inline constexpr uint32_t kStreamVersion = 3;
inline constexpr uint32_t kStreamBlock = 65536;
inline constexpr uint32_t kLegacyBlock = 8u << 20;
inline constexpr uint64_t kMaxStateBytes = 2ull << 30;
inline bool full_state_version_supported(uint32_t version) {
    return version == 1 || version == 2 || version == kStreamVersion;
}
// Apple documents this framing at developer.apple.com/documentation/compression/compression_lz4.
// The fast macOS writer is unchanged; all hosts decode its bv41/bv4-/bv4$ framing.
inline bool decode_apple_lz4(uint8_t* output,size_t raw,const uint8_t* input,size_t encoded) {
    size_t at=0,used=0;
    auto u32=[&](uint32_t& value) {
        if(encoded-at<4)return false;
        value=uint32_t(input[at]) | uint32_t(input[at+1])<<8 |
              uint32_t(input[at+2])<<16 | uint32_t(input[at+3])<<24;
        at+=4;return true;
    };
    while(encoded-at>=4) {
        const auto* tag=input+at;at+=4;
        if(!memcmp(tag,"bv4$",4))return used==raw && at==encoded;
        const bool compressed=!memcmp(tag,"bv41",4);
        if(!compressed && memcmp(tag,"bv4-",4))return false;
        uint32_t decoded=0,stored=0;
        if(!u32(decoded) || decoded>raw-used)return false;
        if(compressed) {
            if(!u32(stored) || !stored || stored>encoded-at ||
               stored>uint32_t(INT32_MAX) || decoded>uint32_t(INT32_MAX))return false;
            const size_t dictionary=std::min<size_t>(used,65536);
            const auto result=LZ4_decompress_safe_usingDict(
                reinterpret_cast<const char*>(input+at),reinterpret_cast<char*>(output+used),
                int(stored),int(decoded),reinterpret_cast<const char*>(output+used-dictionary),int(dictionary));
            if(result!=int(decoded))return false;
        } else {
            stored=decoded;if(stored>encoded-at)return false;
            if(stored)memcpy(output+used,input+at,stored);
        }
        at+=stored;used+=decoded;
    }
    return false; // exact end marker is required
}

class FullStateStreamWriter {
    FILE* file;
    FullStateHeader header;
    std::array<uint8_t,kStreamBlock> raw{};
    std::vector<uint8_t> compressed;
    std::unique_ptr<ZSTD_CCtx,decltype(&ZSTD_freeCCtx)> codec{ZSTD_createCCtx(), ZSTD_freeCCtx};
    size_t used=0;
    bool good=true;
    bool flush() {
        if (!used || !good) return good;
        const auto n=ZSTD_compress2(codec.get(),compressed.data(),compressed.size(),raw.data(),used);
        if (ZSTD_isError(n)) return good=false;
        const uint32_t record[2]={uint32_t(used),uint32_t(n)};
        good=fwrite(record,sizeof record,1,file)==1 && fwrite(compressed.data(),1,n,file)==n;
        header.raw_size+=used; ++header.blocks; used=0;
        return good;
    }
public:
    FullStateStreamWriter(FILE* f,const FullStateHeader& h):file(f),header(h),compressed(ZSTD_compressBound(kStreamBlock)) {
        header.version=kStreamVersion; header.header_size=sizeof header; header.raw_size=0;header.blocks=0;
        good=codec && !ZSTD_isError(ZSTD_CCtx_setParameter(codec.get(),ZSTD_c_compressionLevel,1)) &&
             !ZSTD_isError(ZSTD_CCtx_setParameter(codec.get(),ZSTD_c_checksumFlag,1)) &&
             fwrite(&header,sizeof header,1,file)==1;
    }
    bool bytes(const void* data,size_t size) {
        if (!good || uint64_t(size)+header.raw_size+used>kMaxStateBytes) return good=false;
        auto* p=static_cast<const uint8_t*>(data);
        while(size) {
            const size_t n=std::min(size,raw.size()-used);
            memcpy(raw.data()+used,p,n);used+=n;p+=n;size-=n;
            if (used==raw.size() && !flush())return false;
        }
        return true;
    }
    bool finish() {
        return flush() && good && fseek(file,0,SEEK_SET)==0 && fwrite(&header,sizeof header,1,file)==1 && fflush(file)==0;
    }
    uint64_t raw_size() const {return header.raw_size+used;}
};
inline bool read_full_state_payload(FILE* file,const FullStateHeader& h,std::vector<uint8_t>& payload,std::string& why) {
    if (!full_state_version_supported(h.version)) {why="saved by another version";return false;}
    const uint32_t block=h.version==kStreamVersion?kStreamBlock:kLegacyBlock;
    if (!h.raw_size || h.raw_size>kMaxStateBytes || h.blocks!=(h.raw_size+block-1)/block) {why="invalid state size";return false;}
    const auto start=ftell(file);
    if(start<0 || fseek(file,0,SEEK_END)!=0){why="cannot inspect state";return false;}
    const auto end=ftell(file);
    if(end<start || uint64_t(end-start)<uint64_t(h.blocks)*8 || fseek(file,start,SEEK_SET)!=0){why="file is truncated";return false;}
    auto pass=[&](uint8_t* output) {
    std::vector<uint8_t> encoded;
    std::vector<uint8_t> scratchRaw(output?0:block);
    size_t offset=0;
    for(uint32_t i=0;i<h.blocks;++i) {
        uint32_t record[2]{};
        const size_t raw=std::min<uint64_t>(block,h.raw_size-offset);
        if(fread(record,sizeof record,1,file)!=1 || record[0]!=raw ||
           (h.version==kStreamVersion ? (!record[1] || record[1]>ZSTD_compressBound(raw)) : record[1]>=raw)) {why="invalid state block";return false;}
        const size_t n=record[1]?record[1]:raw;
        const auto at=ftell(file);
        if(at<0 || uint64_t(end-at)<n) {why="file is truncated";return false;}
        encoded.resize(n);
        if(fread(encoded.data(),1,n,file)!=n){why="file is truncated";return false;}
        auto* dst=output?output+offset:scratchRaw.data();
        bool ok=false;
        if(h.version==kStreamVersion) {
            // Our frames are single-segment: their declared content size is also
            // their decoder window. Require checksum and no dictionary, so an
            // untrusted frame cannot request an enormous hidden window.
            ok=n>=5 && (encoded[4]&0x3f)==0x24 &&
               ZSTD_getFrameContentSize(encoded.data(),n)==raw && ZSTD_findFrameCompressedSize(encoded.data(),n)==n;
            if(ok) {const auto decoded=ZSTD_decompress(dst,raw,encoded.data(),n);ok=!ZSTD_isError(decoded)&&decoded==raw;}
        } else if(!record[1]) {memcpy(dst,encoded.data(),raw);ok=true;}
        else if(h.version==1) ok=decode_apple_lz4(dst,raw,encoded.data(),n);
        else ok=LZ4_decompress_safe(reinterpret_cast<const char*>(encoded.data()),reinterpret_cast<char*>(dst),int(n),int(raw))==int(raw);
        if(!ok){why="file is corrupt";return false;}
        offset+=raw;
    }
    if(offset!=h.raw_size || fgetc(file)!=EOF || ferror(file)){why="unexpected state data";return false;}
    return true;
    };
    // Fully validate bounded blocks before allocating the raw snapshot. A tiny
    // malformed/truncated file cannot induce a multi-gigabyte allocation.
    if(!pass(nullptr) || fseek(file,start,SEEK_SET)!=0)return false;
    payload.resize(size_t(h.raw_size));
    return pass(payload.data());
}
} // namespace ss
