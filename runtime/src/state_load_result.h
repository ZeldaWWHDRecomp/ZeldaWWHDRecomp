// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace ss {
// Each asynchronous load carries its own completion through parsing, retries
// and arrival. Copies refer to that request, never a replaceable global slot.
class LoadResult {
    struct State {
        std::mutex mutex;
        std::function<void(bool,const std::string&)> callback;
        bool completed=false;
        explicit State(std::function<void(bool,const std::string&)> cb):callback(std::move(cb)){}
    };
    std::shared_ptr<State> state_;
public:
    LoadResult()=default;
    explicit LoadResult(std::function<void(bool,const std::string&)> cb){
        if(cb)state_=std::make_shared<State>(std::move(cb));
    }
    bool complete(bool ok,const std::string& why)const noexcept {
        if(!state_)return false;
        std::function<void(bool,const std::string&)> cb;
        {
            std::lock_guard lock(state_->mutex);
            if(state_->completed)return false;
            state_->completed=true;cb=std::move(state_->callback);
        }
        // A caller can enqueue another request; no request mutex is held here.
        try{if(cb)cb(ok,why);}catch(...){/* Caller failure cannot escape the game boundary. */}
        return true;
    }
};
}
