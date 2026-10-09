#pragma once
#include <vulkan/vulkan.h>

namespace gfxvk {
// One queue owns all renderer resources. Ranges are deliberately whole-resource;
// readers accumulate until the next write or layout change orders them.
struct ResourceUse {
    VkPipelineStageFlags writeStage = 0, readStages = 0;
    VkAccessFlags writeAccess = 0;
    VkPipelineStageFlags visibleStages = 0;
    VkAccessFlags visibleAccess = 0;
};
struct Dependency {
    bool needed = false;
    VkPipelineStageFlags source = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destination = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkAccessFlags sourceAccess = 0, destinationAccess = 0;
};
constexpr VkAccessFlags resourceWrites = VK_ACCESS_SHADER_WRITE_BIT |
    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

inline Dependency derive_dependency(ResourceUse& use, VkPipelineStageFlags stage,
                                     VkAccessFlags access, bool layoutChange = false) {
    const auto writes = access & resourceWrites;
    const bool covered = (use.visibleStages & stage) == stage &&
                         use.visibleAccess == access;
    Dependency d;
    d.needed = layoutChange || (writes && (use.writeStage || use.readStages)) ||
               (use.writeStage && !covered);
    d.source = use.writeStage | ((writes || layoutChange) ? use.readStages : 0);
    if (!d.source) d.source = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    d.destination = stage ? stage : VkPipelineStageFlags(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    d.sourceAccess = use.writeAccess; // WAR requires execution ordering, no read access mask.
    d.destinationAccess = access;
    if (layoutChange) {
        // A layout transition itself writes image memory. Retain its destination
        // scope as a producer even if the declared next operation is read-only.
        use.readStages = 0;
        use.writeStage |= d.destination;
        use.visibleStages = stage;
        use.visibleAccess = access;
    }
    if (writes) {
        use.writeStage = stage;
        use.writeAccess = writes;
        use.readStages = 0;
        use.visibleStages = 0;
        use.visibleAccess = 0;
    } else {
        use.readStages |= stage;
        if (d.needed) {
            if (use.visibleAccess != access) use.visibleStages = 0;
            use.visibleStages |= stage;
            use.visibleAccess = access;
        }
    }
    return d;
}
} // namespace gfxvk
