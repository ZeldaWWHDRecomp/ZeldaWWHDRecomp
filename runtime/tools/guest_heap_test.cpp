#include "mods/guest_heap.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
#include <vector>
int main() {
    std::vector<uint8_t> memory(4096);
    guestmods::Heap heap(memory.data(),0x7F010000,uint32_t(memory.size()));heap.initialize();
    assert(!heap.allocate(0)&&!heap.allocate(UINT32_MAX));
    std::vector<uint32_t> pointers;std::set<uint32_t> unique;
    for(int i=0;i<64;++i){auto p=heap.allocate(33);assert(p&&!(p&15));assert(unique.insert(p).second);pointers.push_back(p);}
    assert(!heap.release(pointers[0]+1));
    auto snapshot=memory;
    for(auto p:pointers)assert(heap.release(p));
    assert(!heap.release(pointers[0]));
    auto whole=heap.allocate(4080);assert(whole==0x7F010010);assert(!heap.allocate(1));assert(heap.release(whole));
    std::copy(snapshot.begin(),snapshot.end(),memory.begin()); // no host allocator ledger to repair
    assert(!heap.allocate(4096));
    for(auto p:pointers)assert(heap.release(p));
    assert(heap.allocate(4080)==whole);
    memory[8]=0;assert(!heap.allocate(1)&&!heap.release(whole)); // malformed state metadata fails closed
    std::cout<<"guest heap allocation/coalescing/state restore passed\n";
}
