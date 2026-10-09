#include "mods/guest_settings.h"
#include <cassert>
int main() {
    using namespace guestmods::settings;
    Observed settings;
    assert(!settings.sample("unknown",{}));
    auto value=settings.sample("input.face_layout",Data{std::string("position")});
    assert(value&&value->type()==String&&value->revision==1);
    assert(settings.sample("input.face_layout",Data{std::string("position")})->revision==1);
    assert(settings.sample("input.face_layout",Data{std::string("labels")})->revision==2);
    assert(settings.sample("input.face_layout",Data{std::string("position")})->revision==3);
    assert(settings.sample("render.true60",Data{false})->type()==Boolean);
    assert(settings.sample("render.interp_fps",Data{uint32_t(120)})->type()==Unsigned);
    assert(settings.sample("display.aspect",Data{21.0/9})->type()==Number);
    assert(settings.sample("display.aspect",Data{21.0/9})->revision==1);
    assert(settings.sample("display.aspect",Data{32.0/9})->revision==2);
    assert(!settings.sample("unknown",{}));
}
