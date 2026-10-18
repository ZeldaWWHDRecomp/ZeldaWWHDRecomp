#include "overlay/pointer.h"
#include "gfx/stereo.h"
#include "gfx/display_modes.h"
#include <cassert>
#include <cmath>
#include <cstdio>

void log_msg(const char*,...) {}
namespace input { void set_touch(bool,float,float) {} }
namespace aspect { void set_window_aspect(float) {} }

static void near(float a,float b) {
    if(std::fabs(a-b)>=.001f)std::fprintf(stderr,"pointer comparison: %g versus %g\n",a,b);
    assert(std::fabs(a-b)<.001f);
}
static void menu(int mode,float width,float height,float density) {
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize={width/density,height/density};
    io.DisplayFramebufferScale={density,density};io.DeltaTime=1.f/60;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    io.MouseDrawCursor=mode!=stereo::Off;
    int clicks[2]={};float value=.5f;
    auto frame=[&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("pointer test",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        for(int i=0;i<2;++i) {
            ImGui::SetCursorScreenPos({io.DisplaySize.x*(i?.75f:.25f)-20,io.DisplaySize.y*.25f-15});
            if(ImGui::Button(i?"right":"left",{40,30}))++clicks[i];
        }
        ImGui::SetCursorScreenPos({io.DisplaySize.x*.1f,io.DisplaySize.y*.55f});
        ImGui::SetNextItemWidth(io.DisplaySize.x*.8f);
        ImGui::SliderFloat("##depth",&value,0,1);
        ImGui::End();ImGui::Render();
    };
    auto move=[&](float x,float y) {
        auto p=overlay::pointer_position(x,y,io.DisplaySize);
        io.AddMousePosEvent(p.x,p.y);frame();
    };
    frame();
    for(float x:{.25f,.75f}) {
        move(x,.25f);io.AddMouseButtonEvent(0,true);frame();
        io.AddMouseButtonEvent(0,false);frame();
    }
    assert(clicks[0]==1&&clicks[1]==1);
    move(.45f,.55f+8/io.DisplaySize.y);io.AddMouseButtonEvent(0,true);frame();
    move(.49f,.55f+8/io.DisplaySize.y);float before=value;
    move(.51f,.55f+8/io.DisplaySize.y);assert(value>before && value-before<.1f);
    move(.7f,.55f+8/io.DisplaySize.y);assert(value>.65f);
    io.AddMouseButtonEvent(0,false);frame();
    // TAB's horizontal seam must also be continuous.
    move(.4f,.49f);float y=io.MousePos.y;
    move(.4f,.51f);assert(std::fabs((io.MousePos.y-y)-io.DisplaySize.y*.02f)<1.01f);
    if(io.MouseDrawCursor)assert(ImGui::GetForegroundDrawList(ImGui::GetMainViewport())->VtxBuffer.Size>0);
    io.DisplaySize.x*=1.25f;move(.4f,.51f);assert(std::fabs(io.MousePos.x-io.DisplaySize.x*.4f)<1.01f);
    move(-FLT_MAX,-FLT_MAX);assert(!ImGui::IsMousePosValid());
    ImGui::DestroyContext();
}
static void touch(int mode,float width,float height,bool gamepadOnly) {
    gfx::g_has_drc_window=false;gfx::g_shown=true;
    gfx::g_mode=gamepadOnly?gfx::kDrcGamePad:gfx::kDrcPip;
    auto p=gfx::display_plan(true,1280,720,true,854,480,width,height,1);
    if(mode==stereo::FullSBS)p=gfx::display_plan_for(p,width*.5f,height,1280,720,854,480);
    gfx::display_input_plan(p);
    assert(p.pip_on && p.pip.w>0 && p.pip.h>0);
    for(float u:{.01f,.3f,.7f,.99f})for(float v:{.01f,.3f,.7f,.99f}) {
        const float nx=(p.pip.x+u*p.pip.w)/p.dw,ny=(p.pip.y+v*p.pip.h)/p.dh;
        float tx,ty;assert(gfx::overlay_hit(nx,ny,&tx,&ty));near(tx,u);near(ty,v);
        // Cursor and the touched GamePad point occupy the same pixel in BOTH eyes.
        for(int eye:{0,1}) {
            auto cursor=stereo::pack({nx*width,ny*height,0,0},width,height,mode,eye);
            auto box=stereo::pack({p.pip.x/p.dw*width,p.pip.y/p.dh*height,
                p.pip.w/p.dw*width,p.pip.h/p.dh*height},width,height,mode,eye);
            near(cursor.x,box.x+tx*box.w);near(cursor.y,box.y+ty*box.h);
        }
    }
    float tx,ty;
    assert(!gfx::overlay_hit(-.1f,-.1f,&tx,&ty));
    assert(gfx::overlay_hit(-.1f,1.1f,&tx,&ty,true));near(tx,0);near(ty,1);
}
int main() {
    for(int mode:{stereo::HalfSBS,stereo::FullSBS,stereo::TAB})
        for(float aspect:{1.0f,16.f/9,21.f/9,32.f/9}) {
            menu(mode,720*aspect,720,1);menu(mode,1440*aspect,1440,2);
            touch(mode,720*aspect,720,false);touch(mode,720*aspect,720,true);
        }
    std::puts("Stereo pointer: menu clicks, seam-crossing drags, cursor drawing, HiDPI and GamePad touch passed.");
}
