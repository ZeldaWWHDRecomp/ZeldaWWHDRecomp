#include "guest_hud.h"
#include "../mods/guest_hud.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstring>
#include <set>

namespace overlay::guesthud {
namespace {
using namespace guestmods::hud;
struct Texture {
    std::shared_ptr<const Image> image;
    ImTextureData data;
};
std::map<const Image*,std::unique_ptr<Texture>> textures;
std::unique_ptr<ImDrawList> gamepad;
struct CloneDelete {void operator()(ImDrawList* list)const {IM_DELETE(list);}};
std::unique_ptr<ImDrawList,CloneDelete> scaled_gamepad;
ImDrawData gamepad_data;
ImU32 color(uint32_t rgba) {return IM_COL32(rgba>>24,(rgba>>16)&255,(rgba>>8)&255,rgba&255);}
float tv_x=0,tv_y=0,tv_width=0,tv_height=0;
bool tv_visible=true;
void draw(const std::vector<std::shared_ptr<const List>>& lists,ImDrawList* output,float width,float height,float origin_x=0,float origin_y=0) {
    output->PushClipRect(ImVec2(origin_x,origin_y),ImVec2(origin_x+width,origin_y+height),true);
    Blend blend=Blend::Alpha;
    for(const auto& list:lists)for(const auto& command:list->commands) {
        float base_width=list->screen==1?854.f:1280.f,base_height=list->screen==1?480.f:720.f;
        float scale=height/base_height;
        auto point=[&](float x,float y){auto p=anchored(x,y,command.anchor,width,height,base_width,base_height);return ImVec2(origin_x+p.x,origin_y+p.y);};
        if(command.blend!=blend) {
            blend=command.blend;
            output->AddCallback(blend_callback,blend==Blend::Additive?reinterpret_cast<void*>(uintptr_t(1)):nullptr);
        }
        auto a=point(command.x,command.y),b=point(command.x+command.w,command.y+command.h);
        auto rgba=color(command.rgba);
        switch(command.kind) {
        case Command::ClipPush:output->PushClipRect(a,b,true);break;
        case Command::ClipPop:output->PopClipRect();break;
        case Command::Rect:output->AddRectFilled(a,b,rgba);break;
        case Command::RectOutline:output->AddRect(a,b,rgba,0,command.thickness*scale,0);break;
        case Command::Circle:output->AddCircleFilled(a,command.size*scale,rgba,32);break;
        case Command::CircleOutline:output->AddCircle(a,command.size*scale,rgba,32,command.thickness*scale);break;
        case Command::Line:output->AddLine(a,b,rgba,command.thickness*scale);break;
        case Command::Text:
            if(command.size>0)output->AddText(ImGui::GetFont(),command.size*scale,a,rgba,command.text.data(),command.text.data()+command.text.size());
            break;
        case Command::Picture: {
            auto it=textures.find(command.image.get());if(it==textures.end())break;
            ImVec2 center((a.x+b.x)*.5f,(a.y+b.y)*.5f);
            float sine=std::sin(command.rotation),cosine=std::cos(command.rotation);
            auto rotate=[&](ImVec2 p){float x=p.x-center.x,y=p.y-center.y;return ImVec2(center.x+x*cosine-y*sine,center.y+x*sine+y*cosine);};
            output->AddImageQuad(it->second->data.GetTexRef(),rotate(a),rotate(ImVec2(b.x,a.y)),rotate(b),rotate(ImVec2(a.x,b.y)),
                ImVec2(command.u0,command.v0),ImVec2(command.u1,command.v0),ImVec2(command.u1,command.v1),ImVec2(command.u0,command.v1),rgba);
            break;
        }
        }
    }
    if(blend!=Blend::Alpha)output->AddCallback(blend_callback,nullptr);
    output->PopClipRect();
}
}
void blend_callback(const ImDrawList*,const ImDrawCmd*) {}
bool image_texture(uint64_t id) {
    if(id==uint64_t(ImTextureID_Invalid))return false;
    for(const auto& [image,texture]:textures)if(uint64_t(texture->data.GetTexID())==id)return true;
    return false;
}
void set_tv_region(float x,float y,float width,float height,bool visible) {
    tv_x=x;tv_y=y;tv_width=width;tv_height=height;tv_visible=visible;
}
void backend_destroyed() {
    if(ImGui::GetCurrentContext())for(auto& [image,texture]:textures) {
        ImGui::GetPlatformIO().Textures.find_erase(&texture->data);
        ImGui::UnregisterUserTexture(&texture->data);
    }
    textures.clear();gamepad.reset();scaled_gamepad.reset();gamepad_data.Clear();
}
bool active() {return !textures.empty()||store().active();}
void frame() {
    // The port overlay may be open without any guest HUD. Avoid snapshots,
    // allocations and ImGui work then; active() also covers retiring textures.
    if(!active())return;
    auto tv=store().snapshot(0),drc=store().snapshot(1);
    std::set<const Image*> used;
    for(const auto* screen:{&tv,&drc})for(const auto& list:*screen)for(const auto& command:list->commands)
        if(command.image)used.insert(command.image.get());
    for(auto it=textures.begin();it!=textures.end();) {
        auto& texture=it->second->data;
        if(texture.Status==ImTextureStatus_Destroyed&&texture.WantDestroyNextFrame) {
            ImGui::UnregisterUserTexture(&texture);it=textures.erase(it);continue;
        }
        if(used.contains(it->first)) {
            texture.WantDestroyNextFrame=false;
            if(texture.Status==ImTextureStatus_WantDestroy)
                texture.SetStatus(texture.TexID==ImTextureID_Invalid?ImTextureStatus_WantCreate:ImTextureStatus_OK);
        }else texture.WantDestroyNextFrame=true;
        ++it;
    }
    for(const auto* screen:{&tv,&drc})for(const auto& list:*screen)for(const auto& command:list->commands) {
        if(!command.image||textures.contains(command.image.get()))continue;
        auto texture=std::make_unique<Texture>();texture->image=command.image;
        texture->data.Create(ImTextureFormat_RGBA32,int(command.image->width),int(command.image->height));
        texture->data.UseColors=true;
        memcpy(texture->data.GetPixels(),command.image->rgba.data(),command.image->rgba.size());
        ImGui::RegisterUserTexture(&texture->data);textures.emplace(command.image.get(),std::move(texture));
    }
    const auto& io=ImGui::GetIO();
    // Presentation regions are physical pixels; ImGui vertices use logical points.
    float sx=io.DisplayFramebufferScale.x>0?io.DisplayFramebufferScale.x:1;
    float sy=io.DisplayFramebufferScale.y>0?io.DisplayFramebufferScale.y:1;
    if(tv_visible)draw(tv,ImGui::GetBackgroundDrawList(),tv_width>0?tv_width/sx:io.DisplaySize.x,
        tv_height>0?tv_height/sy:io.DisplaySize.y,tv_x/sx,tv_y/sy);
    if(!gamepad)gamepad=std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
    gamepad->_ResetForNewFrame();
    gamepad->PushTexture(ImGui::GetIO().Fonts->TexRef);
    gamepad->PushClipRect(ImVec2(0,0),ImVec2(854,480),false);
    draw(drc,gamepad.get(),854,480);
    gamepad->PopClipRect();gamepad->PopTexture();
}
ImDrawData* gamepad_frame(float width,float height) {
    return gamepad_region(width,height,0,0,width,height,1);
}
ImDrawData* gamepad_region(float target_width,float target_height,float x,float y,float width,float height,float opacity) {
    if(!gamepad||gamepad->VtxBuffer.empty()||width<1||height<1||!store().active())return nullptr;
    scaled_gamepad.reset(gamepad->CloneOutput());
    float scale=std::min(width/854.f,height/480.f),ox=x+(width-854*scale)*.5f,oy=y+(height-480*scale)*.5f;
    for(auto& vertex:scaled_gamepad->VtxBuffer){
        vertex.pos.x=ox+vertex.pos.x*scale;vertex.pos.y=oy+vertex.pos.y*scale;
        auto alpha=uint32_t(((vertex.col>>IM_COL32_A_SHIFT)&255)*std::clamp(opacity,0.f,1.f));
        vertex.col=(vertex.col&~IM_COL32_A_MASK)|(alpha<<IM_COL32_A_SHIFT);
    }
    for(auto& command:scaled_gamepad->CmdBuffer) {
        command.ClipRect.x=ox+command.ClipRect.x*scale;command.ClipRect.z=ox+command.ClipRect.z*scale;
        command.ClipRect.y=oy+command.ClipRect.y*scale;command.ClipRect.w=oy+command.ClipRect.w*scale;
    }
    gamepad_data.Clear();gamepad_data.Valid=true;gamepad_data.DisplaySize=ImVec2(target_width,target_height);
    gamepad_data.FramebufferScale=ImVec2(1,1);gamepad_data.Textures=&ImGui::GetPlatformIO().Textures;
    // CloneOutput contains completed buffers, not live builder write pointers.
    gamepad_data.CmdLists.push_back(scaled_gamepad.get());
    gamepad_data.TotalVtxCount=scaled_gamepad->VtxBuffer.Size;
    gamepad_data.TotalIdxCount=scaled_gamepad->IdxBuffer.Size;
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    gamepad_data.CmdListsCount=1;
#endif
    return &gamepad_data;
}
}
