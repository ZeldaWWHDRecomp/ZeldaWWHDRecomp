// SPDX-License-Identifier: MPL-2.0
#include <atomic>
#include <cassert>
#include <stdexcept>
#include <string>
#include <thread>
#include "state_load_result.h"

int main(){
    unsigned first_calls=0,second_calls=0;
    ss::LoadResult first([&](bool ok,const std::string&why){
        assert(!ok&&why=="native event transaction");++first_calls;
    });
    ss::LoadResult second([&](bool ok,const std::string&why){assert(ok&&why.empty());++second_calls;});
    auto retry=first;
    assert(retry.complete(false,"native event transaction"));
    assert(!first.complete(true,""));
    assert(first_calls==1&&second_calls==0);
    assert(second.complete(true,""));assert(second_calls==1);
    ss::LoadResult nested;
    nested=ss::LoadResult([&](bool,const std::string&){assert(!nested.complete(false,"again"));});
    assert(nested.complete(true,""));
    std::atomic<unsigned> calls=0;
    ss::LoadResult racing([&](bool,const std::string&){++calls;});
    auto copy=racing;
    std::thread other([copy]{copy.complete(false,"refused");});
    racing.complete(true,"");other.join();assert(calls==1);
    ss::LoadResult throwing([](bool,const std::string&){throw std::runtime_error("caller failed");});
    assert(throwing.complete(false,"refused")&&!throwing.complete(true,""));
}
