// Non-mutating, allocation-bounded structural checks for full-state metadata.
#pragma once
#include "savestate.h"
#include <unordered_set>
namespace ss::metadata {
class Cursor {
    Reader reader;
    bool valid=true;
public:
    explicit Cursor(Reader r):reader(r) {}
    size_t remaining() const {
        if (!valid || !reader.ok || !reader.p || !reader.e || reader.e < reader.p) return 0;
        return size_t(reader.e-reader.p);
    }
    bool good() const { return valid && reader.ok; }
    bool end() const { return good() && reader.at_end(); }
    uint32_t u32() { if(remaining()<4){valid=false;return 0;}return reader.u32(); }
    uint64_t u64() { if(remaining()<8){valid=false;return 0;}return reader.u64(); }
    uint8_t u8() { if(!remaining()){valid=false;return 0;}return reader.u8(); }
    bool skip(size_t size) { if(!good() || size>remaining()){valid=false;return false;}return reader.bytes(nullptr,size); }
    template<class T> bool pod(T& value) { if(!good() || sizeof(T)>remaining()){valid=false;return false;}return reader.bytes(&value,sizeof value); }
    // Always divide before multiplying; a file-provided count cannot wrap.
    bool count(uint32_t n,size_t width,size_t tail=0) const {
        return good() && tail<=remaining() && width && n<=(remaining()-tail)/width;
    }
};
inline bool unique(std::unordered_set<uint32_t>& keys,uint32_t value) {
    return keys.insert(value).second;
}
inline bool gx2(Reader reader,uint32_t registerWords) {
    Cursor c(reader);
    if(c.u32()!=registerWords || !c.skip(size_t(registerWords)*4))return false;
    const uint32_t n=c.u32();
    if(!c.count(n,8+size_t(registerWords)*4,16))return false;
    std::unordered_set<uint32_t> keys;
    for(uint32_t i=0;i<n;++i) {
        const uint32_t key=c.u32(),words=c.u32();
        if(!unique(keys,key) || words!=registerWords || !c.skip(size_t(words)*4))return false;
    }
    const auto active=c.u32();c.u32();c.u64();
    return c.end() && (!active || keys.count(active));
}
inline bool heaps(Reader reader) {
    Cursor c(reader);c.u32();c.u32();c.u32();
    const auto expanded=c.u32();
    if(!c.count(expanded,20,4))return false;
    std::unordered_set<uint32_t> heapKeys;
    for(uint32_t i=0;i<expanded;++i) {
        const auto key=c.u32(),start=c.u32(),end=c.u32();
        if(!unique(heapKeys,key) || start>end)return false;
        const auto freeCount=c.u32();
        if(!c.count(freeCount,8,4))return false;
        std::unordered_set<uint32_t> freeKeys;
        for(uint32_t j=0;j<freeCount;++j) {
            const auto addr=c.u32(),size=c.u32();
            if(!unique(freeKeys,addr) || addr<start || uint64_t(addr)+size>end)return false;
        }
        const auto blocks=c.u32();
        if(!c.count(blocks,12))return false;
        std::unordered_set<uint32_t> users;
        for(uint32_t j=0;j<blocks;++j) {
            const auto user=c.u32(),addr=c.u32(),size=c.u32();
            if(!unique(users,user) || addr<start || uint64_t(addr)+size>end ||
               user<addr || uint64_t(user)>=uint64_t(addr)+size)return false;
        }
    }
    const auto frames=c.u32();
    if(!c.count(frames,20))return false;
    heapKeys.clear();
    for(uint32_t i=0;i<frames;++i) {
        const auto key=c.u32(),start=c.u32(),end=c.u32(),head=c.u32(),tail=c.u32();
        if(!unique(heapKeys,key) || !(start<=head && head<=tail && tail<=end))return false;
    }
    return c.end();
}
template<class ThreadRecord>
bool threads(Reader reader) {
    Cursor c(reader);
    const auto records=c.u32();
    if(!c.count(records,sizeof(ThreadRecord),48))return false;
    std::unordered_set<uint32_t> keys;
    for(uint32_t i=0;i<records;++i) {
        ThreadRecord record{};
        if(!c.pod(record) || !unique(keys,record.guest) || record.started>1 ||
           record.exited>1 || record.woken>1 || record.wst>2 || record.wait_kind>10 ||
           record.core>=3 || record.cpu.core>=3 || record.cpu.thread)return false;
    }
    c.u32(); // next guest thread id
    auto n=c.u32();if(!c.count(n,12))return false;keys.clear();
    for(uint32_t i=0;i<n;++i) {
        if(!unique(keys,c.u32()))return false;c.u32();c.u32();
    }
    n=c.u32();if(!c.count(n,10))return false;keys.clear();
    for(uint32_t i=0;i<n;++i) {
        if(!unique(keys,c.u32()) || c.u8()>1 || c.u8()>1)return false;
        const auto waiters=c.u32();if(!c.count(waiters,4) || !c.skip(size_t(waiters)*4))return false;
    }
    n=c.u32();if(!c.count(n,12))return false;keys.clear();
    for(uint32_t i=0;i<n;++i) {
        if(!unique(keys,c.u32()))return false;
        const auto capacity=c.u32(),messages=c.u32();
        if(messages>capacity || !c.count(messages,16) || !c.skip(size_t(messages)*16))return false;
    }
    n=c.u32();if(!c.count(n,8))return false;keys.clear();
    for(uint32_t i=0;i<n;++i) {
        if(!unique(keys,c.u32()))return false;
        const auto waiters=c.u32();if(!c.count(waiters,4) || !c.skip(size_t(waiters)*4))return false;
    }
    n=c.u32();if(!c.count(n,8))return false;keys.clear();
    for(uint32_t i=0;i<n;++i) {if(!unique(keys,c.u32()))return false;c.u32();}
    n=c.u32();if(!c.count(n,36))return false;
    for(uint32_t i=0;i<n;++i) {
        if(c.u32()>=3)return false;c.u32();c.u64();c.u64();c.u32();c.u64();
    }
    n=c.u32();if(!c.count(n,12,16))return false;keys.clear();
    for(uint32_t i=0;i<n;++i) {if(!unique(keys,c.u32()))return false;c.u64();}
    c.u64();c.u64();return c.end();
}
} // namespace ss::metadata
