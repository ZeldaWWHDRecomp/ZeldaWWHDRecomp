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
#include <thread>

#include "guest_addr.h"
#include "runtime.h"
#include "interp.h"

extern "C" void f_02613394_orig(Cpu* c);
extern "C" void f_0260C67C_orig(Cpu* c);
extern "C" void f_0275F808_orig(Cpu* c);
extern "C" void f_0260C74C_orig(Cpu* c);

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
