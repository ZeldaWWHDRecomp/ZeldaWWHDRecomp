// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "guest_addr.h"
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <mutex>
struct Cpu;
namespace story_native_stage_owner_hooks {
// Native function addresses mapped for the current game build.
inline uint32_t SetInfo(){return GC_OPTIONAL(0x0260623C);}
inline uint32_t DeleteInfo(){return GC_OPTIONAL(0x0260757C);}
inline uint32_t FreePackEntry(){return GC_OPTIONAL(0x026126C0);}
inline uint32_t UnloadArchive(){return GC_OPTIONAL(0x0261261C);}
inline uint32_t ManagerDisposer(){return GC_OPTIONAL(0x02613580);}
inline uint32_t ArchiveHolderDtor(){return GC_OPTIONAL(0x02613630);}
inline uint32_t PackHolderDtor(){return GC_OPTIONAL(0x026136A0);}
// Child heaps enter this non-deleting destructor directly during ancestor teardown.
inline uint32_t StageHeapDtor(){return GC_OPTIONAL(0x0275558C);}
inline uint32_t ArchiveResourceDtor(){return GC_OPTIONAL(0x0275D1B4);}
inline uint32_t SarcResourceDtor(){return GC_OPTIONAL(0x0275E1B4);}
// Generic array delete takes the allocation pointer in r3. Observe before
// native heap lookup/free; this is not proof that the allocation is Stage-owned.
inline uint32_t ArrayDelete(){return GC_OPTIONAL(0x0273AFC8);}
struct Args {std::array<uint32_t,4> r3_r6{};};
struct Observer {
    // Before the real SetInfo body can replace a Stage Info tree value without
    // calling DeleteInfo. A future owner must revoke the old receipt here.
    uint64_t (*before_setinfo)(Cpu*, const Args&) = nullptr;
    // Before the original may destroy a backing buffer. Must revoke/fence all
    // uncertain ownership; callbacks must be noexcept and preserve guest CPU.
    uint64_t (*before_destroy)(Cpu*, uint32_t, const Args&) = nullptr;
    // Only after the real original SetInfo returned nonzero. It must validate
    // live native tree, BFRES extent and independent owner before issuing a
    // receipt through finish_setinfo(cookie, verifier).
    void (*after_setinfo)(Cpu*, const Args&, uint32_t result, uint64_t cookie) = nullptr;
    void (*failed_setinfo)(Cpu*, const Args&, uint64_t cookie) = nullptr;
    void (*after_destroy)(Cpu*, uint32_t, const Args&, uint64_t cookie) = nullptr;
    // Minimal fail-closed revoke. Must never throw or call game code.
    void (*fault)() noexcept = nullptr;
    // Optional shared owner fence. Held separately for pre and post callbacks;
    // never held over the native body, which may wait for guest work.
    std::recursive_mutex* lifetime_fence = nullptr;
    // Optional passive raw-free boundary, with the unmodified incoming r3.
    uint64_t (*before_free)(Cpu*, const Args&) = nullptr;
    void (*after_free)(Cpu*, const Args&, uint64_t cookie) = nullptr;
};
bool register_observer(const Observer*) noexcept; // persistent immutable object
bool observed(uint32_t mapped_address) noexcept;
// Outer chain wrapper marks a delegated-original scope; it never treats a
// guest replacement as native execution. Exact native body calls still go
// through invoke_original and are observed once each.
void invoke_dispatch(Cpu*, uint32_t mapped_address, const std::function<void()>& body);
// Call around g_mod_bodies[ordinal] in BOTH ppc_mod_run and call_original.
// Never wrap a guest replacement: it is not proof the native original ran.
void invoke_original(Cpu*, uint32_t mapped_address, const std::function<void()>& body);
} // namespace story_native_stage_owner_hooks
