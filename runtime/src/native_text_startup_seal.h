// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <mutex>
namespace dispatch::detail {
// State policy only; production uses one private instance in core.cpp. No unseal.
// Actual mutators retain lock() from permission check THROUGH the table write.
class StartupSeal {
 mutable std::recursive_mutex mutex_;
 std::atomic<bool> initialized_{false},sealed_{false},fault_{false};
public:
 std::unique_lock<std::recursive_mutex>lock()const{return std::unique_lock<std::recursive_mutex>(mutex_);}
 bool begin_initialize()noexcept{try{auto held=lock();bool no=false;if(!initialized_.compare_exchange_strong(no,true)){fault_=true;return false;}return !fault_.load();}catch(...){fault_=true;return false;}}
 bool writable()noexcept{try{auto held=lock();if(!initialized_.load()||sealed_.load()){fault_=true;return false;}return !fault_.load();}catch(...){fault_=true;return false;}}
 bool seal()noexcept{try{auto held=lock();if(!initialized_.load()||fault_.load())return false;sealed_=true;return true;}catch(...){fault_=true;return false;}}
 bool current()const noexcept{try{auto held=lock();return initialized_.load()&&sealed_.load()&&!fault_.load();}catch(...){return false;}}
};
}
