#include "mods/guest_hud.h"
#include "overlay/guest_hud.h"
#include "imgui.h"
#include <cassert>
#include <cmath>

int main() {
    // No guest HUD must be a no-op even without an ImGui context. Opening the
    // port's own overlay must not initialize guest draw lists or take snapshots.
    assert(!overlay::guesthud::active());
    overlay::guesthud::frame();
    assert(ImGui::GetCurrentContext()==nullptr);
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,720);io.DeltaTime=1.f/60;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    auto& store=guestmods::hud::store();
    assert(!overlay::guesthud::active());
    auto list=store.begin("pilot",1);
    guestmods::hud::Command rect;rect.x=0;rect.y=0;rect.w=854;rect.h=480;
    assert(store.append("pilot",list,rect));assert(store.commit("pilot",list));
    assert(overlay::guesthud::active());
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();
    // A GamePad-only list does not leak into the TV's background geometry.
    assert(ImGui::GetDrawData()->TotalVtxCount==0);
    auto* drc=overlay::guesthud::gamepad_frame(1000,1000);
    assert(drc&&drc->TotalVtxCount==4&&drc->TotalIdxCount==6);
    float miny=1000,maxy=0;
    for(const auto& vertex:drc->CmdLists[0]->VtxBuffer) {
        miny=std::min(miny,vertex.pos.y);maxy=std::max(maxy,vertex.pos.y);
    }
    assert(std::abs(miny-218.96956f)<.01f&&std::abs(maxy-781.03044f)<.01f);
    // Each target gets a fresh copy; scaling must never accumulate.
    drc=overlay::guesthud::gamepad_frame(854,480);
    assert(drc&&drc->CmdLists[0]->VtxBuffer[0].pos.y==0);
    drc=overlay::guesthud::gamepad_region(1920,1080,100,200,640.5f,360,.5f);
    assert(drc&&drc->DisplaySize.x==1920);
    assert(drc->CmdLists[0]->VtxBuffer[0].pos.x==100&&drc->CmdLists[0]->VtxBuffer[0].pos.y==200);
    assert(((drc->CmdLists[0]->VtxBuffer[0].col>>IM_COL32_A_SHIFT)&255)==127);
    store.reset();assert(!overlay::guesthud::active());
    assert(!overlay::guesthud::gamepad_frame(854,480));
    uint8_t pixel[]={255,0,0,255};auto image=store.create_image("pilot",1,1,4,pixel,4);
    list=store.begin("pilot",0);rect.w=rect.h=10;
    assert(store.picture("pilot",list,image,rect)&&store.commit("pilot",list));
    assert(store.release("pilot",image)); // published commands retain the image
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();
    assert(ImGui::GetDrawData()->TotalVtxCount==4);
    bool image_found=false;
    for(auto* texture:ImGui::GetPlatformIO().Textures)if(texture->Width==1&&texture->Height==1) {
        image_found=true;assert(static_cast<const uint8_t*>(texture->GetPixels())[0]==255);
        texture->SetTexID(42);texture->SetStatus(ImTextureStatus_OK);
    }
    assert(image_found&&overlay::guesthud::image_texture(42));store.reset();
    // Retirement continues with no visible mods until the backend acknowledges destruction.
    assert(overlay::guesthud::active());
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();
    for(auto* texture:ImGui::GetPlatformIO().Textures)if(texture->Width==1&&texture->Height==1) {
        texture->SetTexID(ImTextureID_Invalid);texture->SetStatus(ImTextureStatus_Destroyed);
    }
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();
    assert(!overlay::guesthud::active());
    // Primitive geometry, rotated UV subrects and blend transitions share one list.
    list=store.begin("shapes",0);
    rect={};rect.x=100;rect.y=100;rect.w=40;rect.h=20;rect.size=12;rect.thickness=2;
    for(auto kind:{guestmods::hud::Command::Rect,guestmods::hud::Command::RectOutline,
                  guestmods::hud::Command::Circle,guestmods::hud::Command::CircleOutline,guestmods::hud::Command::Line}) {
        rect.kind=kind;assert(store.append("shapes",list,rect));
    }
    rect.kind=guestmods::hud::Command::Text;rect.text="HUD UTF-8";rect.blend=guestmods::hud::Blend::Additive;
    assert(store.append("shapes",list,rect)&&store.commit("shapes",list));
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();
    auto* draw=ImGui::GetDrawData();assert(draw->TotalVtxCount>20);
    unsigned additive=0,alpha=0;
    for(auto* commands:draw->CmdLists)for(const auto& cmd:commands->CmdBuffer)if(cmd.UserCallback==overlay::guesthud::blend_callback)
        (cmd.UserCallbackData?additive:alpha)++;
    assert(additive==1&&alpha==1);
    store.reset();overlay::guesthud::backend_destroyed();assert(!overlay::guesthud::active());
    image=store.create_image("rotation",1,1,4,pixel,4);list=store.begin("rotation",0);
    rect={};rect.x=10;rect.y=20;rect.w=40;rect.h=20;rect.rotation=3.14159265359f/2;
    rect.u0=.25f;rect.v0=.125f;rect.u1=.75f;rect.v1=.875f;
    assert(store.picture("rotation",list,image,rect)&&store.commit("rotation",list));
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();
    draw=ImGui::GetDrawData();assert(draw->TotalVtxCount==4);
    const auto& vertex=draw->CmdLists[0]->VtxBuffer[0];
    assert(std::abs(vertex.pos.x-40)<.001f&&std::abs(vertex.pos.y-10)<.001f);
    assert(vertex.uv.x==.25f&&vertex.uv.y==.125f);
    store.reset();overlay::guesthud::backend_destroyed();
    // TV coordinates are relative to the fitted game region, including wide anchors.
    list=store.begin("anchor",0);rect={};rect.x=1260;rect.y=20;rect.w=10;rect.h=10;
    rect.anchor=guestmods::hud::Anchor::TopRight;assert(store.append("anchor",list,rect)&&store.commit("anchor",list));
    overlay::guesthud::set_tv_region(50,60,1680,720,true);
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();
    draw=ImGui::GetDrawData();assert(draw->TotalVtxCount==4);
    assert(std::abs(draw->CmdLists[0]->VtxBuffer[0].pos.x-1710)<.01f);
    assert(draw->CmdLists[0]->VtxBuffer[0].pos.y==80);
    io.DisplaySize=ImVec2(640,360);io.DisplayFramebufferScale=ImVec2(2,2);
    overlay::guesthud::set_tv_region(0,0,1280,720,true);
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();
    draw=ImGui::GetDrawData();assert(draw->TotalVtxCount==4);
    assert(std::abs(draw->CmdLists[0]->VtxBuffer[0].pos.x-630)<.01f);
    assert(draw->CmdLists[0]->VtxBuffer[0].pos.y==10);
    store.reset();overlay::guesthud::backend_destroyed();
    // Nested clipping changes scissor rectangles, never image geometry/UVs.
    io.DisplaySize=ImVec2(1280,720);io.DisplayFramebufferScale=ImVec2(1,1);
    overlay::guesthud::set_tv_region(0,0,1280,720,true);
    image=store.create_image("clip-a",1,1,4,pixel,4);list=store.begin("clip-a",0);
    rect={};rect.kind=guestmods::hud::Command::ClipPush;rect.x=10;rect.y=20;rect.w=100;rect.h=80;
    assert(store.append("clip-a",list,rect));
    rect.x=20;rect.y=30;rect.w=120;rect.h=90;assert(store.append("clip-a",list,rect));
    rect={};rect.x=10;rect.y=20;rect.w=40;rect.h=20;rect.rotation=3.14159265359f/2;
    assert(store.picture("clip-a",list,image,rect));
    rect={};rect.kind=guestmods::hud::Command::ClipPop;assert(store.append("clip-a",list,rect));
    assert(store.append("clip-a",list,rect));assert(store.commit("clip-a",list));
    auto following=store.begin("clip-b",0);rect={};rect.x=0;rect.y=0;rect.w=5;rect.h=5;
    assert(store.append("clip-b",following,rect)&&store.commit("clip-b",following));
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();draw=ImGui::GetDrawData();
    bool nested=false,restored=false;
    for(auto* commands:draw->CmdLists)for(const auto& cmd:commands->CmdBuffer)if(cmd.ElemCount) {
        if(cmd.ClipRect.x==20&&cmd.ClipRect.y==30&&cmd.ClipRect.z==110&&cmd.ClipRect.w==100)nested=true;
        if(cmd.ClipRect.x==0&&cmd.ClipRect.y==0&&cmd.ClipRect.z==1280&&cmd.ClipRect.w==720)restored=true;
    }
    assert(nested&&restored); // second owner's list cannot inherit the first clip
    const auto& unchanged=draw->CmdLists[0]->VtxBuffer[0];
    assert(std::abs(unchanged.pos.x-40)<.001f&&std::abs(unchanged.pos.y-10)<.001f);
    assert(unchanged.uv.x==0&&unchanged.uv.y==0);
    store.reset();overlay::guesthud::backend_destroyed();
    // DRC region copies scale clip rectangles along with their vertices.
    list=store.begin("clip-drc",1);rect={};rect.kind=guestmods::hud::Command::ClipPush;
    rect.x=10;rect.y=20;rect.w=100;rect.h=80;assert(store.append("clip-drc",list,rect));
    rect={};rect.w=200;rect.h=200;assert(store.append("clip-drc",list,rect));
    rect.kind=guestmods::hud::Command::ClipPop;assert(store.append("clip-drc",list,rect));
    assert(store.commit("clip-drc",list));
    ImGui::NewFrame();overlay::guesthud::frame();ImGui::Render();
    drc=overlay::guesthud::gamepad_region(1920,1080,100,200,427,240,1);assert(drc);
    bool scaled_clip=false;
    for(const auto& cmd:drc->CmdLists[0]->CmdBuffer)if(cmd.ElemCount)
        scaled_clip|=cmd.ClipRect.x==105&&cmd.ClipRect.y==210&&cmd.ClipRect.z==155&&cmd.ClipRect.w==250;
    assert(scaled_clip);store.reset();overlay::guesthud::backend_destroyed();
    ImGui::DestroyContext();
}
