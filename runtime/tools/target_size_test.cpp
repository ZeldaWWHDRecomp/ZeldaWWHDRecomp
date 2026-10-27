#include "gfx/target_size.h"
#include <cassert>

int main() {
    using gfx::TargetSize;
    const gfx::SizeRequest other, chain{TargetSize::Console}, helper{TargetSize::Internal, TargetSize::Console};
    gfx::SizeClaim target;
    target.request(other, 5);
    assert(target.size == TargetSize::Internal);  // an ordinary target
    target.request(helper, 6);
    assert(target.size == TargetSize::Internal);  // a helper pass never claims by itself
    target.request(chain, 100);
    assert(target.size == TargetSize::Console);
    target.request(other, 100);  // another pass into the claimed target, same frame
    assert(target.size == TargetSize::Console);
    target.request(other, 100 + gfx::SizeClaim::kIdleFrames);
    assert(target.size == TargetSize::Console);
    target.request(chain, 105);  // each claim renews it
    target.request(other, 105 + gfx::SizeClaim::kIdleFrames);
    assert(target.size == TargetSize::Console);
    target.request(other, 106 + gfx::SizeClaim::kIdleFrames);  // left by its claimer
    assert(target.size == TargetSize::Internal);  // another target at the address
    target.request(chain, 121);  // claimed again
    assert(target.size == TargetSize::Console && target.frame == 121);

    // the pause menu: the chain is idle for ~80 frames, then its helper pass draws the level before
    // the blur does in the same frame. The helper renews the claim, so the level is not resized.
    target.request(helper, 201);
    assert(target.size == TargetSize::Console && target.frame == 201);
    target.request(chain, 201);
    assert(target.size == TargetSize::Console);
    target.request(other, 201 + gfx::SizeClaim::kIdleFrames + 1);  // released as before
    assert(target.size == TargetSize::Internal);
}
