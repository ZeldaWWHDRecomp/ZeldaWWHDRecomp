// story_notebook_test: the notebook without its page layout (story_notebook_layout.h): the page is drawn
// on the HUD, as in a session without a layout. The layout itself is tested in game runs.
#include <cstdint>
#include <string>

#include "mods/story_notebook_layout.h"
#include "mods/story_notebook_page.h"

namespace story_notebook {
void set_layout_description(const std::string&, const std::string&) {}
void set_icon_open(const std::string&) {}
const std::vector<uint8_t>& icon_open_png() {
    static const std::vector<uint8_t> none;
    return none;
}
}  // namespace story_notebook
namespace story_notebook_layout {
bool test_layout_ready = false;
bool test_layout_drawn = false;
bool configure() { return false; }
bool update(const story_notebook::Page& page, const std::vector<story_notebook::Row>&) {
    const bool active = test_layout_ready && page.open();
    test_layout_drawn |= active;
    return active;
}
bool update_closed() { return test_layout_drawn; }
void reset() { test_layout_drawn = false; }
bool drawing() { return test_layout_drawn; }
bool animating() { return false; }
}  // namespace story_notebook_layout

// World actors/scenes are separate integration services; notebook tests use ordinary residents.
struct Cpu;
namespace story_actors {
bool people_message(Cpu*) { return false; }
bool people_next_message(Cpu*) { return false; }
bool people_execute(Cpu*,bool) { return false; }
bool owns_resident(uint32_t) { return false; }
}
namespace story_scenes { void trigger(const std::string&,const std::string&,const std::string&,unsigned) {} }
