// Host-only ownership. No guest pointers, native transforms or serialized state.
#pragma once
#include <cstdint>
#include <optional>
namespace guestmods::maphud {
enum Status : int32_t { Unsupported=-1, Invalid=-2, Busy=-3, Phase=-4 };
class CompassLease {
    uint32_t owner_=0;
    bool renewed_=false, releasing_=false;
    double remaining_=0;
public:
    void clear(){owner_=0;renewed_=releasing_=false;remaining_=0;}
    void begin(double dt,bool eligible){
        renewed_=false;
        if(!eligible){clear();return;}
        if(releasing_){remaining_-=dt;if(remaining_<=1e-9)clear();}
    }
    int request(uint32_t owner,bool replace,bool eligible){
        if(!owner)return Invalid;
        if(owner_&&owner_!=owner)return Busy;
        if(!replace){
            if(owner_&&!releasing_){releasing_=true;remaining_=.2;}
            return 0;
        }
        if(!eligible){clear();return 0;}
        owner_=owner;renewed_=true;releasing_=false;remaining_=0;return 1;
    }
    void finish(){if(owner_&&!renewed_&&!releasing_)clear();}
    void revoke(uint32_t owner){if(owner_==owner)clear();}
    bool held()const{return owner_!=0;}
    uint32_t owner()const{return owner_;}
};
struct Selection {uint32_t owner,index;uint64_t generation;};
class ChartQueue {
    std::optional<Selection> pending_;
public:
    int select(uint32_t owner,uint32_t index,int selected,uint64_t generation){
        if(!owner||index>=49||!generation)return Invalid;
        if(pending_&&pending_->owner!=owner)return Busy;
        if(int(index)==selected){pending_.reset();return 0;}
        pending_=Selection{owner,index,generation};return 1;
    }
    int cancel(uint32_t owner){
        if(pending_&&pending_->owner!=owner)return Busy;
        bool had=pending_.has_value();pending_.reset();return had?1:0;
    }
    void clear(){pending_.reset();}
    void revoke(uint32_t owner){if(pending_&&pending_->owner==owner)clear();}
    // Native input wins over a request that has not committed. A busy carousel
    // keeps the latest same-owner request, without interrupting its current art.
    std::optional<Selection> take(uint64_t generation,bool idle,bool native_changed){
        if(pending_&&(pending_->generation!=generation||native_changed))clear();
        if(!idle||!pending_)return {};
        auto out=pending_;clear();return out;
    }
    const std::optional<Selection>& pending()const{return pending_;}
};
} // namespace guestmods::maphud
