// SPDX-License-Identifier: MPL-2.0
#include "native_quiescence.h"
#include <exception>
#include <utility>
#include <limits>
namespace threads {
namespace {thread_local size_t native_pins=0,read_only_pins=0;void add_pin(){if(native_pins==std::numeric_limits<size_t>::max())std::terminate();++native_pins;}void remove_pin(){if(!native_pins)std::terminate();--native_pins;}}
bool current_thread_holds_native_quiescence()noexcept{return native_pins!=0;}
bool current_thread_holds_read_only_native_quiescence()noexcept{return read_only_pins!=0;}
QuiescenceWitness::QuiescenceWitness(std::function<bool(const Cpu*)>c):current_(std::move(c)),thread_(std::this_thread::get_id()){}
bool QuiescenceWitness::current(const Cpu*c)const noexcept{try{return thread_==std::this_thread::get_id()&&current_&&current_(c);}catch(...){return false;}}

NativeQuiescence::NativeQuiescence(std::function<bool(const Cpu*)>c,std::function<void()>r,bool read_only):current_(std::move(c)),release_(std::move(r)),thread_(std::this_thread::get_id()),counted_(bool(release_)),read_only_(read_only){if(counted_){add_pin();if(read_only_)++read_only_pins;}}
void NativeQuiescence::release()noexcept{
 if(!release_)return;if(thread_!=std::this_thread::get_id())std::terminate();
 auto release=std::move(release_);current_={};release_={};try{release();}catch(...){std::terminate();}if(counted_){if(read_only_){if(!read_only_pins)std::terminate();--read_only_pins;}remove_pin();counted_=false;}
}
NativeQuiescence::NativeQuiescence(NativeQuiescence&&o)noexcept:current_(std::move(o.current_)),release_(std::move(o.release_)),thread_(o.thread_),counted_(o.counted_),read_only_(o.read_only_){if(counted_&&thread_!=std::this_thread::get_id())std::terminate();o.current_={};o.release_={};o.counted_=false;}
NativeQuiescence&NativeQuiescence::operator=(NativeQuiescence&&o)noexcept{if(this!=&o){release();if(o.release_&&o.thread_!=std::this_thread::get_id())std::terminate();current_=std::move(o.current_);release_=std::move(o.release_);thread_=o.thread_;counted_=o.counted_;read_only_=o.read_only_;o.current_={};o.release_={};o.counted_=false;}return *this;}
NativeQuiescence::~NativeQuiescence(){release();}
bool NativeQuiescence::current(const Cpu*c)const noexcept{try{return thread_==std::this_thread::get_id()&&current_&&current_(c);}catch(...){return false;}}
}
