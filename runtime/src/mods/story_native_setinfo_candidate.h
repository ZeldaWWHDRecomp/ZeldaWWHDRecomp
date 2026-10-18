// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_sarc_extent.h"
#include <array>
#include <cstdint>

namespace story_native_setinfo_candidate {
using Read = story_native_sarc_extent::Read;
struct Layout {
    uint32_t manager_vtable{}, info_vtable{}, wrapper_vtable{};
    uint32_t holder_vtable{}, archive_resource_vtable{};
    uint32_t info_node_vtable{}, resource_node_vtable{};
    uint32_t solid_heap_vtable{};
};
struct Candidate {
    uint32_t info{}, archive_wrapper{}, archive_holder{}, archive_resource{};
    uint32_t outer_base{}, outer_bytes{}, archive_base{}, archive_bytes{};
    uint32_t resource{}, resource_bytes{}, entry{};
    uint32_t archive_key{}, resource_key{};
    uint32_t request_heap{}, data_heap{}, data_heap_parent{}, pack_heap{};
    // Native Resource::setBuffer records ownership in resource+0x20 bit 0.
    // This observes the current branch's buffer lifetime mode, not an
    // independently proven allocation or a publication lease.
    bool archive_buffer_owned{};
    std::array<char,64> stage_name{};
    bool operator==(const Candidate&) const = default;
};

// Read-only post-SetInfo identity check. `control`, `archive_name` and `path`
// are original r3/r4/r5 guest arguments; `manager` is the current res-manager
// global value. A callback must additionally establish a complete unload
// fence before using this candidate to issue a stage-owner receipt.
bool verify(const Read& read, const Layout& layout, uint32_t control,
            uint32_t archive_name, uint32_t path, uint32_t manager,
            Candidate& out);
// Recheck a captured candidate after SetInfo's original argument objects may
// have expired. This is still only a read-only identity check, not a lifetime
// fence or an owner receipt.
bool verify_live(const Read& read, const Layout& layout, uint32_t control,
                 uint32_t manager, const Candidate& expected, Candidate& out);
} // namespace story_native_setinfo_candidate
