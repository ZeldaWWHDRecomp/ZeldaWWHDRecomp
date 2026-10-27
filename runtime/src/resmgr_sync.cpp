// HD ResMgr: an archive request taken "from the packs" is finished before the load task looks at it
// (docs/decomp-notes.md, "ResMgr pack requests finish before the load task continues").
//
// ResLoadTask::startLoad (0260C4CC) creates an ExpHeap with all the free space of its parent and asks
// ResMgr for the archive with ResMgr_requestObjectAsync (026127D4) or ResMgr_requestStageAsync
// (02612908). When ResMgr is busy (request state +0x11248 != 0, e.g. 2 while ResMgrPackLoadThread loads
// the permanent packs during boot), the request only sets "from pack" (+0x112D8) and ResMgrThread
// (02613394) runs ResMgr_loadFromPack (02610258) without touching the request state. ResLoadTask::
// waitLoad (0260C67C) waits only while the state is 1, so on the next frame it shrinks the heap
// (ExpHeap adjust) and registers the archive, whether or not ResMgrThread is done. On the console
// ResMgrThread (priority 14, its own core) always finishes within that frame. In the port it is a host
// thread that the host OS may run late (a busy phone during boot); then adjust cut the heap after
// the archive's buffer, the 0x44-byte SharcArchiveRes allocation failed and 0275D21C (setBuffer) wrote
// through a null pointer (signal 11 at guest address 00000014).
//
// Here the "from pack" byte doubles as "ResMgrThread still has the request": ResMgrThread clears it
// once its load returns, and waitLoad reports "not yet" while it is set. The game's flow and data stay
// the console's; only the order the console guarantees by timing is enforced. The flag lives in guest
// memory, so save states need nothing extra.
//
// Test only (tools/gametest/resmgr_boot_race.sh): WWHD_TEST_RESMGR_DELAY_MS=ms delays ResMgrThread
// before each "from pack" load (a late host thread), WWHD_TEST_RESMGR_MID_DELAY_MS=ms in the middle of
// it (hook_0275F808 below); WWHD_RESMGR_NO_WAIT=1 turns the wait off (the old behaviour, to show the crash);
// WWHD_RESMGR_LOG=1 logs each "from pack" request.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <mutex>
#include <set>
#include <thread>

#include "guest_addr.h"
#include "runtime.h"
#include "interp.h"
#include "resmgr_fit.h"
#include "mods/guest_heap_query.h"

extern "C" void f_02613394_orig(Cpu* c);
extern "C" void f_0260C67C_orig(Cpu* c);
extern "C" void f_0275F808_orig(Cpu* c);
extern "C" void f_0260C74C_orig(Cpu* c);
extern "C" void f_02612E64_orig(Cpu* c);
extern "C" void f_02610258_orig(Cpu* c);
extern "C" void f_02520460_orig(Cpu* c);

namespace {
constexpr uint32_t kRequestState = 0x11248;  // 1 request, 2 permanent packs, 0 idle
constexpr uint32_t kRequestName = 0x1124C;   // FixedSafeString<48>: +0 top
constexpr uint32_t kFromPack = 0x112D8;      // u8

int env_int(const char* name) {
    const char* e = getenv(name);
    return e ? atoi(e) : 0;
}
const bool g_log = env_int("WWHD_RESMGR_LOG") != 0;
const bool g_no_wait = env_int("WWHD_RESMGR_NO_WAIT") != 0;
const int g_delay_ms = env_int("WWHD_TEST_RESMGR_DELAY_MS");
const int g_mid_delay_ms = env_int("WWHD_TEST_RESMGR_MID_DELAY_MS");
thread_local bool t_from_pack = false;  // ResMgrThread is in a "from pack" load
std::atomic<int> g_waits{0};
}  // namespace

// ResMgrThread message handler (self, thread, message)
extern "C" void hook_02613394(Cpu* c) {
    const uint32_t self = c->r[3], msg = c->r[5];
    if (msg != 1 || !self || !ld8(self + kFromPack)) {
        f_02613394_orig(c);
        return;
    }
    if (g_log)
        LOG("[resmgr] \"%s\" from the packs (state %u)", mem::read_cstr(ld32(self + kRequestName)).c_str(),
            ld32(self + kRequestState));
    if (g_delay_ms > 0) {  // the host thread runs late: its emulated core stays free meanwhile
        BlockingScope b;
        std::this_thread::sleep_for(std::chrono::milliseconds(g_delay_ms));
    }
    t_from_pack = true;
    f_02613394_orig(c);
    t_from_pack = false;
    std::atomic_thread_fence(std::memory_order_release);  // the archive is in the map before the flag clears
    st8(self + kFromPack, 0);
}

// ResLoadTask::waitLoad (this, request entry): 1 when the request is done
extern "C" void hook_0260C67C(Cpu* c) {
    const uint32_t mgr = ld32(GD(0x101F4F7C));
    if (!g_no_wait && mgr && ld8(mgr + kFromPack)) {
        if (g_log && g_waits.fetch_add(1) < 20) LOG("[resmgr] load task waits for ResMgrThread");
        c->r[3] = 0;
        return;
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    f_0260C67C_orig(c);
    if (g_log && g_no_wait && c->r[3] && mgr && ld8(mgr + kFromPack))
        LOG("[resmgr] load task went on while ResMgrThread still had the request");
}

// sead archive resource read (resource, buffer, size), called by ResMgr_loadFromPack after it allocated
// the archive buffer. Test only: WWHD_TEST_RESMGR_MID_DELAY_MS=ms pauses ResMgrThread after the read of
// a "from pack" load, i.e. before the SharcArchiveRes allocation (the reported crash point); its core
// stays free meanwhile.
extern "C" void hook_0275F808(Cpu* c) {
    f_0275F808_orig(c);
    if (t_from_pack && g_mid_delay_ms > 0) {
        BlockingScope b;
        std::this_thread::sleep_for(std::chrono::milliseconds(g_mid_delay_ms));
    }
}

// ResLoadTask::calc (0260C74C): a sead task of its own, calced by the framework on every frame like
// the game's frame task (the per-frame function 0203593C), and before it. Each calc moves the load
// queue one step on (startLoad, waitLoad -> finish, next), so with frame interpolation's hold
// passes an archive (an island's LOD model: lod_bg, a stage's archives on a scene change) was ready
// one or more logic steps earlier than at 30 fps, and the game's logic took another course. On hold
// passes the load task waits: it calcs once per 30 Hz step, in the frame of the step's logic, as at
// 30 fps. debug: WWHD_INTERP_LOAD_STEP=0 calcs it on every frame as before
extern "C" void hook_0260C74C(Cpu* c) {
    static const bool every_frame = getenv("WWHD_INTERP_LOAD_STEP") && !atoi(getenv("WWHD_INTERP_LOAD_STEP"));
    if (!every_frame && interp::frame_is_hold()) return;
    f_0260C74C_orig(c);
}

// ---------------------------------------------------------------- an archive from a pack fits its heap (resmgr_fit.h)
// Test only: WWHD_TEST_RESMGR_REFUSE=name[,name...] refuses those archives as if their heap were full (the refusal's
// path through the game, without filling a heap first).
namespace {
// archives the check below refused, until a later load of the same name fits (ResMgrThread writes, the game's
// actors read: dComIfG_resLoad below)
std::mutex g_refused_mu;
std::set<std::string> g_refused;
bool test_refused(const std::string& name) {
    static const std::string list = [] { const char* e = getenv("WWHD_TEST_RESMGR_REFUSE"); return e ? "," + std::string(e) + "," : std::string(); }();
    return !list.empty() && list.find("," + name + ",") != std::string::npos;
}
// The game's heaps (sead): the request's heap, or with heap 0 the thread's current heap (HeapMgr::getCurrentHeap
// 02756140, as the game's operator new takes it). A load task's request heaps are frame (solid) heaps (vtable
// 10145B38): free is a no-op there, so a probe that allocated and freed kept the archive's whole size taken and the
// real buffer allocation then failed (the very crash the check exists for). On a frame heap the probe's allocations
// are undone by putting its head and tail (+0x94, +0x98: everything an allocation changes) back as they were;
// another heap frees them itself (vtable slot 0x3C, free(heap, address), as the game's operator delete does).
constexpr uint32_t kFrameHeapHead = 0x94, kFrameHeapTail = 0x98;
uint32_t probe_heap(Cpu* c, uint32_t heap) { return heap ? heap : guest_call(c, GC(0x02756140), {ld32(GD(0x101F8B4C))}); }
bool frame_heap(uint32_t heap) { return guest_heap::readable(heap, kFrameHeapTail + 4) && ld32(heap + guest_heap::kVtable) == GD(0x10145B38); }
void release(Cpu* c, uint32_t heap, uint32_t address) {
    const uint32_t vt = guest_heap::readable(heap, 0x10) ? ld32(heap + guest_heap::kVtable) : 0;
    const uint32_t fn = guest_heap::readable(vt, 0x40) ? ld32(vt + 0x3C) : 0;
    if (fn) guest_call(c, fn, {heap, address});
}
// ResMgr_loadArchive / ResMgr_loadFromPack (self, name, path, heap): false when the archive is in a loaded pack, not
// loaded yet, and the heap can't hold the game's copy of it
bool pack_archive_fits(Cpu* c, const char* who) {
    // heap 0: the load task could not make the request's heap (its parent is full); the game's allocations then take
    // the thread's current heap, and so does the probe
    const uint32_t self = c->r[3], name = c->r[4], heap = c->r[6];
    if (!self || !name) return true;  // the game's own path
    const Cpu keep = *c;
    // loaded already: the game returns at once
    if (guest_call(c, GC(0x02613F20), {self + 0x14, ld32(self + 0x14), name})) return *c = keep, true;
    // the pack entry, found as the game finds it: its name's hash (the C string up to 0x40000 characters)
    const uint32_t text = ld32(name);
    if (!text) return *c = keep, true;
    uint32_t length = 0;
    while (length <= 0x40000 && ld8(text + length)) ++length;
    if (length > 0x40000) length = 0;
    const uint32_t hash = guest_call(c, GC(0x0273B264), {text, length});
    const uint32_t sp = c->r[1];
    c->r[1] = sp - 0x20;  // a frame of our own for the key
    st32(c->r[1], sp);
    const uint32_t key = c->r[1] + 0x10;
    st32(key, hash);
    const uint32_t node = guest_call(c, GC(0x0260ACC8), {self + 0xD228, ld32(self + 0xD228), key});
    c->r[1] = sp;
    if (!node) return *c = keep, true;  // not in a pack: the file system path, which checks its allocations
    const uint32_t holder = ld32(node + 0x18), resource = holder ? ld32(holder + 0x4C) : 0;
    if (!resource) return *c = keep, true;
    const uint32_t size = guest_call(c, GC(0x0275F800), {resource});
    const std::string archive = mem::read_cstr(text);
    resmgr_fit::Fit fit = resmgr_fit::Fit::NoBuffer;
    const uint32_t owner = probe_heap(c, heap);
    if (!test_refused(archive) && owner) {
        const bool frame = frame_heap(owner);
        const uint32_t head = frame ? ld32(owner + kFrameHeapHead) : 0, tail = frame ? ld32(owner + kFrameHeapTail) : 0;
        const uint32_t before = g_log ? guest_heap::free_size(c, owner, guest_call) : 0;
        fit = resmgr_fit::probe([&](uint32_t bytes, uint32_t, uint32_t align) { return guest_call(c, GC(0x0273B0D4), {bytes, owner, align}); },
                                [&](uint32_t address) { if (!frame) release(c, owner, address); }, owner, size);
        if (frame) st32(owner + kFrameHeapHead, head), st32(owner + kFrameHeapTail, tail);
        if (g_log) LOG("[resmgr] %s %s (%u bytes): heap %08X%s, %u free before the check and %u after (%s)", who, archive.c_str(), size, owner,
                       frame ? " (frame heap)" : "", before, guest_heap::free_size(c, owner, guest_call), resmgr_fit::name(fit));
    }
    *c = keep;
    std::lock_guard lk(g_refused_mu);
    if (fit == resmgr_fit::Fit::Fits) {
        g_refused.erase(archive);
        return true;
    }
    g_refused.insert(archive);
    static std::set<std::string> said;
    if (said.insert(archive).second)
        LOG("[resmgr] %s: archive %s (%u bytes) is not loaded: its heap %08X is full (%s)%s; what uses it is not created", who, archive.c_str(), size,
            heap, resmgr_fit::name(fit), test_refused(archive) ? " [test]" : "");
    return false;
}
}  // namespace

// ResMgr_loadArchive (self, name, path, heap, compressed, align, buffer align): 0 when it is not loaded
extern "C" void hook_02612E64(Cpu* c) {
    if (!pack_archive_fits(c, "load")) {
        c->r[3] = 0;
        return;
    }
    f_02612E64_orig(c);
}

// ResMgr_loadFromPack (self, name, path, heap): 0 when it is not loaded
extern "C" void hook_02610258(Cpu* c) {
    if (!pack_archive_fits(c, "load from pack")) {
        c->r[3] = 0;
        return;
    }
    f_02610258_orig(c);
}

// dComIfG_resLoad (phase, archive name): 4 when the archive is loaded, 5 on an error. The load task's request
// "completes" also when the check above refused its archive: the phase is then done (id 2) without an archive in
// the resource table (ResourceControlHD 101F4F28, archives at +0x14), and a game actor went on to its heap and
// asserted on a missing model ("model_data != 0", OSPanic), and its delete (dComIfG_resDelete -> deleteRes 02607138)
// would read the missing table node. For an archive refused here, the phase fails instead, as for a missing
// archive: the request's reference on the load task (ResLoadTask 101F4F54, its reference tree +0xE0, a node's count
// +0x18) is given back (on the last one the request's id is forgotten, 0260F61C, as deleteRes does), the phase goes
// back to 0 so dComIfG_resDelete releases nothing, and the actor's create gets 5 and is not made. A later create
// requests the archive anew.
extern "C" void hook_02520460(Cpu* c) {
    const uint32_t phase = c->r[3], name = c->r[4];
    f_02520460_orig(c);
    if (c->r[3] != 4 || !phase || !name || ld32(phase + 4) != 2) return;
    const std::string archive = mem::read_cstr(name);
    {
        std::lock_guard lk(g_refused_mu);
        if (!g_refused.count(archive)) return;
    }
    const Cpu keep = *c;
    const uint32_t hash = guest_call(c, GC(0x0273B264), {name, uint32_t(archive.size())});
    const uint32_t sp = c->r[1];
    c->r[1] = sp - 0x20;  // a frame of our own for the key
    st32(c->r[1], sp);
    const uint32_t key = c->r[1] + 0x10;
    st32(key, hash);
    const uint32_t control = ld32(GD(0x101F4F28));
    const bool mounted = control && guest_call(c, GC(0x0260ACC8), {control + 0x14, ld32(control + 0x14), key});
    if (!mounted) {
        const uint32_t task = ld32(GD(0x101F4F54));
        if (const uint32_t node = task ? guest_call(c, GC(0x0260ACC8), {task + 0xE0, ld32(task + 0xE0), key}) : 0) {
            const uint32_t count = ld32(node + 0x18);
            if (count > 1) st32(node + 0x18, count - 1);
            else guest_call(c, GC(0x0260F61C), {task, hash});
        }
    }
    c->r[1] = sp;
    *c = keep;
    if (mounted) return;
    st32(phase + 4, 0);
    c->r[3] = 5;
    static std::set<std::string> said;
    std::lock_guard lk(g_refused_mu);
    if (said.insert(archive).second) LOG("[resmgr] %s was refused: what loads it is not created", archive.c_str());
}
