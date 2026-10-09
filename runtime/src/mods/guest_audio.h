// Bounded guest PCM queues. Only host-owned sample copies cross the service boundary.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace guestmods::pcm {
constexpr uint32_t kStreams=32,kPerMod=4,kFrames=8192,kMaxSubmit=2048;
constexpr int32_t kInvalid=-1,kBusy=-2,kQuota=-3;
class Store {
    struct Stream {
        std::string owner;
        uint32_t handle=0,channels=0,read=0,count=0;
        std::array<int16_t,kFrames*2> samples{};
    };
    std::array<Stream,kStreams> streams_{};
    std::mutex mutex_;
    uint64_t epoch_=1;
    uint32_t next_=1;
    std::atomic<bool> active_{false};
    Stream* find(const std::string& owner,uint32_t handle) {
        if(!handle)return nullptr;
        for(auto& s:streams_)if(s.handle==handle&&s.owner==owner)return &s;
        return nullptr;
    }
public:
    bool active() const {return active_.load(std::memory_order_relaxed);}
    uint64_t epoch() {std::lock_guard lock(mutex_);return epoch_;}
    int32_t open(const std::string& owner,uint32_t rate,uint32_t channels) {
        if(owner.empty()||rate!=48000||(channels!=1&&channels!=2))return kInvalid;
        std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return kBusy;
        uint32_t count=0;for(auto& s:streams_)count+=s.handle&&s.owner==owner;
        // Never recycle numeric handles: a restored guest cannot alias a later stream.
        if(count>=kPerMod||next_>0x7fffffff)return kQuota;
        for(auto& s:streams_)if(!s.handle){s.owner=owner;s.handle=next_++;s.channels=channels;s.read=s.count=0;active_.store(true,std::memory_order_relaxed);return s.handle;}
        return kQuota;
    }
    int32_t available(const std::string& owner,uint32_t handle) {
        std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return kBusy;
        auto* s=find(owner,handle);return s?int32_t(kFrames-s->count):kInvalid;
    }
    // Samples are native-endian on this internal interface; guest bridge decodes BE.
    int32_t submit(const std::string& owner,uint32_t handle,const int16_t* data,uint32_t frames,uint32_t channels) {
        if(frames>kMaxSubmit||(!data&&frames))return kInvalid;
        std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return kBusy;
        auto* s=find(owner,handle);if(!s||channels!=s->channels)return kInvalid;
        uint32_t n=std::min(frames,kFrames-s->count);
        for(uint32_t i=0;i<n;++i)for(uint32_t ch=0;ch<2;++ch)
            s->samples[((s->read+s->count+i)%kFrames)*2+ch]=data[i*channels+(channels==1?0:ch)];
        s->count+=n;return n;
    }
    int32_t close(const std::string& owner,uint32_t handle) {
        std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return kBusy;
        auto* s=find(owner,handle);if(!s)return kInvalid;s->handle=0;s->owner.clear();s->count=0;return 0;
    }
    void drop(const std::string& owner) {
        std::lock_guard lock(mutex_);for(auto& s:streams_)if(s.owner==owner){s.handle=0;s.owner.clear();s.count=0;}
    }
    void reset() {
        std::lock_guard lock(mutex_);for(auto& s:streams_){s.handle=0;s.owner.clear();s.count=0;}active_.store(false,std::memory_order_relaxed);++epoch_;
    }
    // Called by AX producer, not the device callback. No allocation or waiting.
    // Missing input is silence; muted streams still drain without delayed playback.
    void mix(int16_t* stereo,uint32_t frames,bool muted=false) {
        if(!active())return;
        std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return;
        for(uint32_t i=0;i<frames;++i) {
            int32_t left=stereo[i*2],right=stereo[i*2+1];
            for(auto& s:streams_)if(s.handle&&s.count) {
                if(!muted){left+=s.samples[s.read*2];right+=s.samples[s.read*2+1];}
                s.read=(s.read+1)%kFrames;--s.count;
            }
            stereo[i*2]=int16_t(std::clamp(left,-32768,32767));
            stereo[i*2+1]=int16_t(std::clamp(right,-32768,32767));
        }
    }
};
inline Store& store(){static Store instance;return instance;}
}
