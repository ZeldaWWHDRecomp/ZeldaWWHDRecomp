// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_camera_policy.h"
namespace story_native_camera_capture {
using Read=story_native_camera_policy::Read;
using Camera=story_native_event_observation::Camera;
using Expected=story_native_event_observation::Expected;
struct Layout {
 uint32_t play{},profile0{},profile1{},methods{};
 std::array<uint32_t,5> functions{};
 story_native_camera_policy::Tables tables{};
 // play + camera: dComIfGp_getCamera(0)'s process (canonical +0x5AF8; callers pass GI(0x5AF8), the Japanese
 // build moved it)
 uint32_t camera;
 Layout(uint32_t play,uint32_t profile0,uint32_t profile1,uint32_t methods,
  std::array<uint32_t,5> functions,story_native_camera_policy::Tables tables,uint32_t camera)
  :play(play),profile0(profile0),profile1(profile1),methods(methods),
   functions(functions),tables(tables),camera(camera){}
};
// Relocated Layout values are supplied by the production adapter. Read must
// independently bound actual guest memory. Two stable raw samples authenticate
// the native camera instance and immutable metadata. Mutable type/style/cache
// tuples are intentionally captured even midway through fixed detach writes;
// this is NOT camera-policy/transition verification or ownership authority.
// No writes, invocation, hook installation, lease or release capability.
bool capture(const Read&,const Layout&,const Expected&,Camera& out);
}
