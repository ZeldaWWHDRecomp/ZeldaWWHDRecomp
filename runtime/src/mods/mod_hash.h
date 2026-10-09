#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace mods::hash {
// Streaming SHA-256 (FIPS 180-4) of a native library: the confirmation is bound to these exact bytes.
inline std::string sha256_stream(std::istream& f){
    static constexpr uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    auto rotr=[](uint32_t x,int n){return (x>>n)|(x<<(32-n));};
    auto block=[&](const unsigned char* b){
        uint32_t w[64],v[8];for(int i=0;i<16;i++)w[i]=uint32_t(b[4*i])<<24|uint32_t(b[4*i+1])<<16|uint32_t(b[4*i+2])<<8|uint32_t(b[4*i+3]);
        for(int i=16;i<64;i++)w[i]=w[i-16]+(rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10));
        std::copy(h,h+8,v);
        for(int i=0;i<64;i++){uint32_t t1=v[7]+(rotr(v[4],6)^rotr(v[4],11)^rotr(v[4],25))+((v[4]&v[5])^(~v[4]&v[6]))+k[i]+w[i],t2=(rotr(v[0],2)^rotr(v[0],13)^rotr(v[0],22))+((v[0]&v[1])^(v[0]&v[2])^(v[1]&v[2]));std::copy_backward(v,v+7,v+8);v[4]+=t1;v[0]=t1+t2;}
        for(int i=0;i<8;i++)h[i]+=v[i];};
    std::vector<unsigned char> buffer(1<<16);unsigned char pending[128];size_t used=0;uint64_t total=0;
    while(f){f.read(reinterpret_cast<char*>(buffer.data()),std::streamsize(buffer.size()));size_t n=size_t(f.gcount());total+=n;
        for(size_t i=0;i<n;){size_t take=std::min<size_t>(64-used,n-i);std::copy_n(buffer.data()+i,take,pending+used);used+=take;i+=take;if(used==64){block(pending);used=0;}}}
    if(!f.eof())throw std::runtime_error("Cannot read file for SHA-256");
    pending[used++]=0x80;size_t end=used<=56?64:128;std::fill(pending+used,pending+end,0);
    for(int i=0;i<8;i++)pending[end-1-i]=uint8_t((total*8)>>(8*i));
    block(pending);if(end==128)block(pending+64);
    std::string hex;const char* digits="0123456789abcdef";for(uint32_t word:h)for(int shift=28;shift>=0;shift-=4)hex+=digits[(word>>shift)&15];
    return hex;
}
inline std::string sha256_file(const std::filesystem::path& p) {
    std::ifstream input(p,std::ios::binary);if(!input)throw std::runtime_error("Cannot read file for SHA-256");
    return sha256_stream(input);
}
inline std::string sha256_text(const std::string& text) {
    std::istringstream input(text);return sha256_stream(input);
}
}
