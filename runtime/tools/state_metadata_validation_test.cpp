#include "state_metadata_validation.h"
#include <cassert>
#include <climits>
#include <functional>
using ss::Writer;
using ss::Reader;
namespace {
struct ThreadFixture {
    uint32_t guest=0x1000;
    uint8_t started=1,exited=0,wst=1,wait_kind=2,woken=0;
    uint32_t core=1;
    struct { uint32_t core=1;void* thread=nullptr; } cpu;
};
void checked(const Writer& writer,const std::function<bool(Reader)>& validate) {
    assert(validate(Reader(writer.b.data(),writer.b.size())));
    // Every incomplete prefix must fail BEFORE any restoring code is called.
    for(size_t n=0;n<writer.b.size();++n)
        assert(!validate(Reader(writer.b.data(),n)));
    auto extra=writer;extra.u8(0);
    assert(!validate(Reader(extra.b.data(),extra.b.size())));
}
Writer gx2() {
    Writer w;w.u32(4);for(int i=0;i<4;++i)w.u32(i);
    w.u32(1);w.u32(7);w.u32(4);for(int i=0;i<4;++i)w.u32(i+9);
    w.u32(7);w.u32(0);w.u64(900);return w;
}
Writer heaps() {
    Writer w;w.u32(0x100);w.u32(0x200);w.u32(0x300);
    w.u32(1);w.u32(0x400);w.u32(0x440);w.u32(0x500);
    w.u32(1);w.u32(0x480);w.u32(0x80);
    w.u32(1);w.u32(0x440);w.u32(0x440);w.u32(0x40);
    w.u32(1);w.u32(0x600);w.u32(0x640);w.u32(0x700);w.u32(0x660);w.u32(0x680);
    return w;
}
Writer threads() {
    Writer w;w.u32(1);w.pod(ThreadFixture{});w.u32(2);
    w.u32(1);w.u32(0x2000);w.u32(0x1000);w.u32(1); // mutex
    w.u32(1);w.u32(0x3000);w.u8(1);w.u8(0);w.u32(1);w.u32(0x1000); // event
    w.u32(1);w.u32(0x4000);w.u32(2);w.u32(1);for(int i=0;i<4;++i)w.u32(i); // queue
    w.u32(1);w.u32(0x5000);w.u32(1);w.u32(0x1000); // sleep
    w.u32(1);w.u32(0x6000);w.u32(3); // rendezvous
    w.u32(1);w.u32(1);w.u32(0x7000);w.u64(100);w.u64(10);w.u32(0x8000);w.u64(9); // alarm
    w.u32(1);w.u32(0x7000);w.u64(9);w.u64(10);w.u64(90);return w;
}
void patch32(Writer& w,size_t offset,uint32_t value) {assert(offset+4<=w.b.size());memcpy(w.b.data()+offset,&value,4);}
}
int main() {
    auto g=gx2(),h=heaps(),t=threads();
    auto vg=[](Reader r){return ss::metadata::gx2(r,4);};
    auto vh=[](Reader r){return ss::metadata::heaps(r);};
    auto vt=[](Reader r){return ss::metadata::threads<ThreadFixture>(r);};
    checked(g,vg);checked(h,vh);checked(t,vt);
    auto malformed=g;patch32(malformed,20,UINT_MAX);assert(!vg(Reader(malformed.b.data(),malformed.b.size())));
    malformed=g;patch32(malformed,28,3);assert(!vg(Reader(malformed.b.data(),malformed.b.size())));
    malformed=g;patch32(malformed,48,8);assert(!vg(Reader(malformed.b.data(),malformed.b.size())));
    malformed=h;patch32(malformed,12,UINT_MAX);assert(!vh(Reader(malformed.b.data(),malformed.b.size())));
    malformed=h;patch32(malformed,28,UINT_MAX);assert(!vh(Reader(malformed.b.data(),malformed.b.size())));
    malformed=h;patch32(malformed,36,UINT_MAX);assert(!vh(Reader(malformed.b.data(),malformed.b.size())));
    malformed=t;patch32(malformed,0,UINT_MAX);assert(!vt(Reader(malformed.b.data(),malformed.b.size())));
    // Empty legacy tables remain valid; state machines, no hardcoded object caps.
    Writer emptyThreads;for(int i=0;i<9;++i)emptyThreads.u32(0);emptyThreads.u64(0);emptyThreads.u64(0);
    checked(emptyThreads,vt);
    Writer emptyHeaps;for(int i=0;i<5;++i)emptyHeaps.u32(0);checked(emptyHeaps,vh);
    // Real allocator edges: an empty managed range retains a zero-size free
    // entry, and a zero-size frame allocation may leave head/tail at end.
    Writer endHeaps;endHeaps.u32(1);endHeaps.u32(2);endHeaps.u32(3);
    endHeaps.u32(1);endHeaps.u32(0x400);endHeaps.u32(0x440);endHeaps.u32(0x440);
    endHeaps.u32(1);endHeaps.u32(0x440);endHeaps.u32(0);endHeaps.u32(0);
    endHeaps.u32(1);endHeaps.u32(0x500);endHeaps.u32(0x540);endHeaps.u32(0x600);
    endHeaps.u32(0x600);endHeaps.u32(0x600);checked(endHeaps,vh);
    // OP_SETUP_CONTEXT accepts key zero; the saved active sentinel remains zero.
    auto zeroContext=g;patch32(zeroContext,24,0);patch32(zeroContext,48,0);checked(zeroContext,vg);
    // Invalid host core or retained host pointer must never reach thread restore.
    malformed=t;patch32(malformed,4+offsetof(ThreadFixture,core),3);
    assert(!vt(Reader(malformed.b.data(),malformed.b.size())));
    constexpr auto tail=8+sizeof(ThreadFixture);
    malformed=t;patch32(malformed,tail+26,UINT_MAX);
    assert(!vt(Reader(malformed.b.data(),malformed.b.size())));
    malformed=t;patch32(malformed,tail+46,3);
    assert(!vt(Reader(malformed.b.data(),malformed.b.size())));
    malformed=t;
    const auto duplicate=std::vector<uint8_t>(t.b.begin()+4,t.b.begin()+4+sizeof(ThreadFixture));
    malformed.b.insert(malformed.b.begin()+4,duplicate.begin(),duplicate.end());
    patch32(malformed,0,2);
    assert(!vt(Reader(malformed.b.data(),malformed.b.size())));
}
