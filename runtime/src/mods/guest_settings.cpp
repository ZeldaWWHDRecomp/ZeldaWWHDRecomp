#include "guest_settings.h"
#include "game_settings.h"
#include "../input_map.h"
#include "../input.h"
#include "../aspect.h"
#include "../interp.h"
#include "../gfx/display_modes.h"
#include "../console_language.h"
#include "guest_addr.h"

namespace guestmods::settings {
std::optional<Value> read(const std::string& key) {
    std::optional<Data> data;
    if(key=="input.face_layout") {
        auto layout=input_map::face_layout(input_map::current());
        data=std::string(layout==input_map::FaceLayout::kPosition?"position":layout==input_map::FaceLayout::kLabels?"labels":"custom");
    }else if(key=="input.controller_mode")data=std::string(input::pro_controller()?"pro":"gamepad");
    else if(key=="game.language")data=uint32_t(console_language());
    else if(key=="game.build")data=std::string(g_guest_build_name);
    else if(key=="display.drc_mode") {
        int mode=gfx::g_mode.load(std::memory_order_relaxed);
        if(mode>=0&&mode<gfx::kDrcModeCount)data=std::string(gfx::kModeNames[mode]);
    }else if(key=="display.aspect")data=double(aspect::game());
    else if(key=="render.interp_fps")data=uint32_t(interp::mode()==1?interp::output_fps():interp::mode()==2?60:30);
    else if(key=="render.true60")data=interp::mode()==2;
    else if(key.rfind("settings.",0)==0) {  // the item menu page's settings (game_settings.h): toggles bool, choices u32, numbers f64
        if(const game_settings::Item* item=game_settings::find(key.substr(9));item&&item->owner=="port"&&item->get) {
            const float v=item->get();
            if(item->kind==game_settings::Kind::Toggle)data=v>=0.5f;
            else if(item->kind==game_settings::Kind::Choice)data=uint32_t(v<0?0:v+0.5f);
            else data=double(v);
        }
    }
    static Observed observed;
    return observed.sample(key,std::move(data));
}
}
