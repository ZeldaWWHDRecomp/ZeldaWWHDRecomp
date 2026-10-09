// Guest-side allocator metadata lives entirely in the mod region, so states restore the allocator.
#pragma once
#include <cstdint>
#include <mutex>
namespace guestmods {
class Heap {
    uint8_t* bytes_;
    uint32_t base_, size_;
    std::mutex mutex_;
    static constexpr uint32_t header=16, magic=0x57484850;
    uint32_t get(uint32_t p) const { return uint32_t(bytes_[p])<<24|uint32_t(bytes_[p+1])<<16|uint32_t(bytes_[p+2])<<8|bytes_[p+3]; }
    void put(uint32_t p,uint32_t v) { for(int i=0;i<4;++i)bytes_[p+i]=uint8_t(v>>(24-8*i)); }
    bool block(uint32_t p) const {
        return p<=size_&&size_-p>=header&&get(p+8)==magic&&get(p)>=header&&
               !(get(p)&15)&&get(p)<=size_-p&&get(p+4)<=1;
    }
    void set(uint32_t p,uint32_t size,bool used) { put(p,size);put(p+4,used);put(p+8,magic);put(p+12,0); }
public:
    Heap(uint8_t* bytes,uint32_t base,uint32_t size):bytes_(bytes),base_(base),size_(size&~15u) {}
    void initialize() { std::lock_guard lock(mutex_);if(size_>=header)set(0,size_,false); }
    uint32_t allocate(uint32_t bytes) {
        std::lock_guard lock(mutex_);
        if(!bytes||bytes>size_||bytes>UINT32_MAX-31)return 0;
        uint32_t need=(bytes+31)&~15u;
        for(uint32_t p=0;p<size_;) {
            if(!block(p))return 0;uint32_t n=get(p);
            if(!get(p+4)&&n>=need) {
                if(n-need>=header+16){set(p+need,n-need,false);n=need;}
                set(p,n,true);return base_+p+header;
            }
            p+=n;
        }
        return 0;
    }
    bool release(uint32_t address) {
        std::lock_guard lock(mutex_);
        if(!address)return true;
        for(uint32_t p=0,previous=UINT32_MAX;p<size_;) {
            if(!block(p))return false;uint32_t n=get(p);
            if(address==base_+p+header) {
                if(!get(p+4))return false;
                uint32_t next=p+n;
                if(next<size_){if(!block(next))return false;if(!get(next+4))n+=get(next);}
                set(p,n,false);
                if(previous!=UINT32_MAX&&!get(previous+4))set(previous,get(previous)+n,false);
                return true;
            }
            previous=p;p+=n;
        }
        return false;
    }
};
} // namespace guestmods
