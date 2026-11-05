// No GPU/window: execute the production pending-scope policy.
#include "../src/gfx/metal_backdrop_source.h"
#include <cassert>
#include <vector>
int main() {
 gfx::gamepad_backdrop::ScanCache cache;
 std::vector<unsigned> pending;
 // Already committed display data is deliberately outside this pending helper.
 cache.capture(1,10);assert(cache.scan(1).ticket==10);
 const unsigned committedTexture=10;
 // A first successful pane remains pending until a second empty/failed scope.
 cache.capture(1,11);pending.push_back(11);
 gfx::metal_backdrop_finish_scope(false,cache,pending);
 assert(pending.empty()&&committedTexture==10);
 assert(cache.scan(1).ticket==0);
 // Successful scopes preserve the exact pending selection.
 cache.capture(1,12);pending.push_back(12);
 gfx::metal_backdrop_finish_scope(true,cache,pending);
 assert(pending.size()==1&&cache.scan(1).ticket==12);
 // Failure must clear other pending targets too, a conservative fail-closed rule.
 cache.capture(2,13);pending.push_back(13);
 gfx::metal_backdrop_finish_scope(false,cache,pending);
 assert(pending.empty()&&cache.scan(2).ticket==0);
}
