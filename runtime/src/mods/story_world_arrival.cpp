// SPDX-License-Identifier: MPL-2.0
#include "story_world_arrival.h"
#include "story_mail_book.h"
#include "overlay/extensions.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
namespace story_world_arrival { namespace {
std::mutex mutex;
Queue queue;
bool registered_layer=false;
std::atomic<bool> any_declared{false};
double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
bool Queue::push(const std::string& text,float seconds){
    std::u16string checked;
    if(text.empty()||text.size()>kMaxTextBytes||!std::isfinite(seconds)||seconds<1||seconds>10||
       !story_mail::append_utf16(text,false,checked)||pending_.size()>=kMaxPending)return false;
    pending_.push_back({text,seconds});return true;
}
std::string Queue::current(double time){
    if(!std::isfinite(time))return {};
    if(!active_.empty()&&time<expires_)return active_;
    active_.clear();
    if(!pending_.empty()){auto notice=std::move(pending_.front());pending_.pop_front();active_=std::move(notice.text);expires_=time+notice.seconds;}
    return active_;
}
void Queue::clear(){pending_.clear();active_.clear();expires_=0;}
bool show(const std::string& text,float seconds){std::lock_guard lock(mutex);return queue.push(text,seconds);}
void state_loaded(){std::lock_guard lock(mutex);queue.clear();}
void note_declared(){any_declared.store(true,std::memory_order_release);}
bool declared(){return any_declared.load(std::memory_order_acquire);}
bool registered(){return registered_layer;}
void register_overlay(){
    if(registered_layer)return;
    if(overlay::ext::sealed())return;
    overlay::ext::add_frame_layer({[](std::string& toast){std::lock_guard lock(mutex);auto text=queue.current(now());if(!text.empty())toast=std::move(text);return false;},[](float,float){return false;}});
    registered_layer=true;
}
}
