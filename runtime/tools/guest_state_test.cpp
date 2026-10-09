// Full-state mod memory and identity metadata, entirely synthetic and never written to disk.
#include "mods/guest_mods.h"
#include "mods/guest_heap.h"
#include "mods/guest_state_section.h"
#include "state_memory.h"
#include "full_state_header.h"
#include <cassert>
#include <iostream>
int main() {
    std::vector<guestmods::ModIdentity> saved={{"a","1.0.0"},{"b","2.0.0"}},read;
    ss::Writer identities;guestmods::save_mod_set(identities,saved);
    assert(guestmods::read_mod_set(ss::Reader(identities.b.data(),identities.b.size()),read));
    assert(!guestmods::different_mods(saved,read));
    std::reverse(read.begin(),read.end());assert(!guestmods::different_mods(saved,read));
    read[0].version="3.0.0";assert(guestmods::different_mods(saved,read));
    read.clear();assert(guestmods::read_mod_set(ss::Reader(nullptr,0),read));
    assert(guestmods::different_mods(saved,read));
    assert(!guestmods::read_mod_set(ss::Reader(identities.b.data(),identities.b.size()-1),read));
    std::vector<uint8_t> memory(guestmods::kRegionSize);
    auto pointer=[&](uint32_t a){assert(a>=guestmods::kRegionStart&&a<guestmods::kRegionStart+memory.size());return memory.data()+a-guestmods::kRegionStart;};
    auto touched=[](const uint8_t*,size_t){return true;};
    guestmods::Heap heap(memory.data()+0x10000,guestmods::kRegionStart+0x10000,0x10000);heap.initialize();
    auto allocation=heap.allocate(64);assert(allocation);pointer(allocation)[0]=123;memory[0]=42; // code/data sentinel
    std::vector<ss::MemoryRegion> regions={{guestmods::kRegionStart,guestmods::kRegionSize}};
    ss::Writer snapshot;ss::capture_regions(snapshot,regions,pointer,touched);
    // The CPU/controller extension ends at byte 104; both mod sections follow it,
    // rather than occupying bytes in the historical 96-byte prefix.
    static_assert(sizeof(ss::FullStateHeader)==104);
    for(unsigned mode:{1u,2u}) {
        ss::FullStateHeader header{};memcpy(header.magic,"WWHDSTAT",8);
        header.version=1;header.header_size=sizeof header;header.controller=mode;
        ss::Writer payload;payload.u64(identities.b.size());payload.bytes(identities.b.data(),identities.b.size());
        payload.u64(snapshot.b.size());payload.bytes(snapshot.b.data(),snapshot.b.size());
        header.raw_size=payload.b.size();
        FILE* file=tmpfile();assert(file);
        assert(fwrite(&header,sizeof header,1,file)==1);
        assert(fwrite(payload.b.data(),payload.b.size(),1,file)==1);rewind(file);
        ss::FullStateHeader restored{};std::string why;
        assert(ss::read_full_state_header(file,restored,why));
        assert(ftell(file)==104&&restored.controller==mode&&restored.raw_size==payload.b.size());
        std::vector<uint8_t> bytes(restored.raw_size);assert(fread(bytes.data(),bytes.size(),1,file)==1);fclose(file);
        ss::Reader sections(bytes.data(),bytes.size());auto n=sections.u64();
        std::vector<guestmods::ModIdentity> mods;
        assert(guestmods::read_mod_set(ss::Reader(sections.p,n),mods)&&!guestmods::different_mods(saved,mods));
        assert(sections.bytes(nullptr,n));n=sections.u64();
        ss::Reader memory_section(sections.p,n);ss::MemoryChunks restored_chunks;std::vector<ss::MemoryRegion> restored_regions;
        assert(ss::read_regions(memory_section,restored_regions,restored_chunks));
        assert(restored_regions[0].base==guestmods::kRegionStart&&restored_chunks.at(guestmods::kRegionStart)[0]==42);
        assert(sections.bytes(nullptr,n)&&sections.at_end());
    }
    ss::Reader reader(snapshot.b.data(),snapshot.b.size());ss::MemoryChunks chunks;std::vector<ss::MemoryRegion> parsed;
    assert(ss::read_regions(reader,parsed,chunks)&&parsed.size()==1&&chunks.size()==2);
    assert(heap.release(allocation));memory[0]=0;memory[0x30000]=99;
    ss::restore_regions(parsed,chunks,pointer,touched);
    assert(memory[0]==42&&pointer(allocation)[0]==123&&memory[0x30000]==0);
    assert(heap.release(allocation)); // allocator state restored with guest memory
    auto malformed=snapshot.b;uint32_t invalid_index=guestmods::kRegionSize/ss::kMemoryChunk;
    memcpy(malformed.data()+16,&invalid_index,4);ss::Reader bad(malformed.data(),malformed.size());
    parsed.clear();chunks.clear();assert(!ss::read_regions(bad,parsed,chunks));
    std::cout<<"guest state region and mismatch metadata passed\n";
}
