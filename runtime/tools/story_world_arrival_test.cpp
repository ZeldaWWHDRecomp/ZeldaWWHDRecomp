#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
// SPDX-License-Identifier: MPL-2.0
#include "mods/story_world_arrival.h"
#include "overlay/extensions.h"
namespace {bool sealed=false;std::vector<overlay::ext::FrameLayer> layers;}
namespace overlay::ext {bool sealed(){return ::sealed;}void add_frame_layer(FrameLayer layer){layers.push_back(std::move(layer));}}
int main(){using namespace story_world_arrival;
 Queue q;assert(!q.push("")&&!q.push("bad\ntext")&&!q.push(std::string(97,'x'))&&!q.push("bad\xC0\x80")&&!q.push("island",NAN)&&!q.push("island",0));
 assert(q.push("Île",2)&&q.push("Next",1));assert(q.current(100)=="Île"&&q.current(101.999)=="Île"&&q.current(102)=="Next"&&q.current(103).empty());
 for(unsigned i=0;i<kMaxPending;++i)assert(q.push("Island"));assert(!q.push("Overflow"));q.clear();assert(q.current(1000).empty());
 // Inert until a package declares an arrival: main() registers no layer before then.
 assert(!declared()&&!registered()&&layers.empty());note_declared();assert(declared()&&!registered()&&layers.empty());
 // Registration is dormant; it uses the existing notice rendering and never captures input.
 register_overlay();assert(registered());register_overlay();assert(layers.size()==1);std::string toast="Saved";assert(!layers[0].wanted(toast)&&toast=="Saved");
 assert(show("New island"));toast.clear();assert(!layers[0].wanted(toast)&&toast=="New island");state_loaded();toast="Saved";assert(!layers[0].wanted(toast)&&toast=="Saved");
}
