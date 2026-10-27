// SPDX-License-Identifier: MPL-2.0
// Render targets kept at another size than the internal resolution while a pass claims them.
#pragma once
#include <cstdint>

namespace gfx {
// Console: the bloom chain's levels (issue #307)
enum class TargetSize : uint8_t { Internal, Console };

// A draw's request for its target's size: `claim` the size its pass needs, or `renews` a claim of
// that size that is already there (a helper pass of the claiming one, which never claims by itself)
struct SizeRequest {
    TargetSize claim = TargetSize::Internal;
    TargetSize renews = TargetSize::Internal;
};

// A render target's size, claimed by the draws of the passes that need it. Other passes draw into
// those targets too (a generic pass at PS 44FD8500 writes the bloom chain's levels every frame), so
// a draw without a claim returns the target to the internal resolution only once the claim is a
// few frames old: the game can reuse the address for another target of the same size and format.
struct SizeClaim {
    static constexpr uint64_t kIdleFrames = 10;
    TargetSize size = TargetSize::Internal;
    uint64_t frame = 0;  // of the last claim

    void request(SizeRequest r, uint64_t now) {
        if (r.claim != TargetSize::Internal) {
            size = r.claim;
            frame = now;
        } else if (size != TargetSize::Internal && r.renews == size) {
            frame = now;
        } else if (size != TargetSize::Internal && now - frame > kIdleFrames) {
            size = TargetSize::Internal;
        }
    }
};
}  // namespace gfx
