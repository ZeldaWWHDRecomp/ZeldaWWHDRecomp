// SPDX-License-Identifier: MPL-2.0
// HD ResMgr: an archive taken from a loaded pack only when its load heap holds it (issue #281).
//
// ResMgr_loadArchive (02612E64) and ResMgr_loadFromPack (02610258) copy an archive out of a loaded pack
// into the request's heap: a buffer of its size rounded up to 32 bytes (alignment 0x2000), the
// SharcArchiveRes (0x44 bytes, alignment 0x2000) and its holder (0x54 bytes, alignment 4). Neither
// checks the buffer: on a full heap the pack's decompressor (0275EBC0) writes the archive to guest
// address 0 (the reported crash: "write of address 0000200000000000 (guest address 00000000)" under
// 0275ED14 <- 0275F808 <- 02612E64 on ResMgrThread). On the console the room's heap always held the
// game's own archives; with a mod's models, buildings and placed objects in the same room it can be
// full, and then the archive is not loaded: the load fails as a missing archive does (its actor is not
// created), instead of writing through a null pointer.
//
// probe() tries the game's three allocations in its order with the game's own allocator and gives them
// back (nothing else allocates from the request heap meanwhile). The load task's request heaps are frame
// heaps, where free does nothing: the caller (resmgr_sync.cpp) puts such a heap's head and tail back as
// they were instead, and release() is then a no-op.
#pragma once
#include <cstdint>
#include <initializer_list>

namespace resmgr_fit {
inline constexpr uint32_t kBufferAlign = 0x2000, kResourceBytes = 0x44, kResourceAlign = 0x2000, kHolderBytes = 0x54, kHolderAlign = 4;
// the archive buffer as the game sizes it
inline uint32_t buffer_bytes(uint32_t size) { return (size + 0x1F) & ~0x1Fu; }
enum class Fit { Fits, NoBuffer, NoResource, NoHolder };
inline const char* name(Fit f) {
    switch (f) {
    case Fit::Fits: return "fits";
    case Fit::NoBuffer: return "no room for its buffer";
    case Fit::NoResource: return "no room for its archive resource";
    case Fit::NoHolder: return "no room for its holder";
    }
    return "?";
}
// alloc(bytes, heap, align) -> address or 0 (sead operator new with a heap), release(address)
template <class Alloc, class Release>
Fit probe(Alloc alloc, Release release, uint32_t heap, uint32_t size) {
    const uint32_t buffer = alloc(buffer_bytes(size), heap, kBufferAlign);
    if (!buffer) return Fit::NoBuffer;
    const uint32_t resource = alloc(kResourceBytes, heap, kResourceAlign);
    const uint32_t holder = resource ? alloc(kHolderBytes, heap, kHolderAlign) : 0;
    if (holder) release(holder);
    if (resource) release(resource);
    release(buffer);
    return !resource ? Fit::NoResource : !holder ? Fit::NoHolder : Fit::Fits;
}
}  // namespace resmgr_fit
