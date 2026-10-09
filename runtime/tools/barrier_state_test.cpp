#include "gfx/vulkan/barrier_state.h"
#include <cassert>
#include <cstdio>
using namespace gfxvk;
int main() {
    constexpr auto transfer = VK_PIPELINE_STAGE_TRANSFER_BIT;
    constexpr auto vertex = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    constexpr auto fragment = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    constexpr auto compute = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    ResourceUse u;
    auto d = derive_dependency(u, transfer, VK_ACCESS_TRANSFER_WRITE_BIT, true);
    assert(d.needed && d.source == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT && !d.sourceAccess);
    d = derive_dependency(u, fragment, VK_ACCESS_SHADER_READ_BIT, true);
    assert(d.needed && (d.source & transfer) && d.sourceAccess == VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(d.destination == fragment && d.destinationAccess == VK_ACCESS_SHADER_READ_BIT);
    d = derive_dependency(u, fragment, VK_ACCESS_SHADER_READ_BIT);
    assert(!d.needed); // same-layout, already-visible read
    d = derive_dependency(u, vertex, VK_ACCESS_SHADER_READ_BIT);
    assert(d.needed && (d.source & transfer)); // newly participating reader needs visibility
    d = derive_dependency(u, compute, VK_ACCESS_SHADER_WRITE_BIT);
    assert(d.needed && (d.source & fragment) && (d.source & vertex));
    assert(!(d.sourceAccess & VK_ACCESS_SHADER_READ_BIT));
    d = derive_dependency(u, transfer, VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(d.needed && d.source == compute && d.sourceAccess == VK_ACCESS_SHADER_WRITE_BIT);
    u = {};
    assert(!derive_dependency(u, vertex, VK_ACCESS_SHADER_READ_BIT).needed);
    assert(!derive_dependency(u, fragment, VK_ACCESS_SHADER_READ_BIT).needed);
    d = derive_dependency(u, transfer, VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(d.needed && d.source == (vertex | fragment) && !d.sourceAccess); // pure WAR
    u = {};
    derive_dependency(u, fragment, VK_ACCESS_SHADER_READ_BIT);
    d = derive_dependency(u, transfer, VK_ACCESS_TRANSFER_READ_BIT, true);
    assert(d.needed && (d.source & fragment) && !d.sourceAccess); // read/read layout change
    d = derive_dependency(u, vertex, VK_ACCESS_SHADER_READ_BIT, true);
    assert(d.needed && (d.source & transfer)); // layout writes must be ordered too
    // Attachment LOAD/blend/stencil read+write: source masks contain writes only.
    u = {};
    constexpr auto tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    derive_dependency(u, tests, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    d = derive_dependency(u, fragment, VK_ACCESS_SHADER_READ_BIT, true);
    assert(d.source == tests && d.sourceAccess == VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    // Visibility for different access classes cannot be combined into a false cross-product.
    u = {};
    derive_dependency(u, compute, VK_ACCESS_SHADER_WRITE_BIT);
    derive_dependency(u, fragment, VK_ACCESS_SHADER_READ_BIT);
    derive_dependency(u, transfer, VK_ACCESS_TRANSFER_READ_BIT);
    assert(derive_dependency(u, fragment, VK_ACCESS_TRANSFER_READ_BIT).needed);
    std::puts("barrier derivation PASS (single queue; no ownership transfers)");
}
