// Renderer-independent guest HUD storage. Guest buffers are copied before publication.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace guestmods::hud {
using Handle = uint32_t;
inline constexpr uint32_t kMaxCommands=1024, kMaxImages=32, kMaxLists=2, kMaxVertices=32768;
inline constexpr size_t kMaxPixels=16*1024*1024, kMaxText=64*1024;
struct Image { uint32_t width,height; std::vector<uint8_t> rgba; };
enum class Anchor : uint32_t { Center,TopLeft,Top,TopRight,Left,Right,BottomLeft,Bottom,BottomRight };
enum class Blend : uint32_t { Alpha,Additive };
struct Point {float x,y;};
// Preserve height-based game coordinates, extending the horizontal virtual canvas.
// Anchors shift the authored 16:9 edge to the corresponding displayed edge.
inline Point anchored(float x,float y,Anchor anchor,float width,float height,float base_width,float base_height) {
    float scale=height/base_height,extra=(width/scale-base_width)*.5f;
    float shift=0;
    if(anchor==Anchor::TopLeft||anchor==Anchor::Left||anchor==Anchor::BottomLeft)shift=-extra;
    if(anchor==Anchor::TopRight||anchor==Anchor::Right||anchor==Anchor::BottomRight)shift=extra;
    return {(extra+x+shift)*scale,y*scale};
}
struct Command {
    enum Kind { Rect,Text,Picture,RectOutline,Circle,CircleOutline,Line } kind=Rect;
    float x=0,y=0,w=0,h=0,size=0;
    float thickness=1,rotation=0,u0=0,v0=0,u1=1,v1=1;
    Anchor anchor=Anchor::Center;
    Blend blend=Blend::Alpha;
    uint32_t rgba=0xFFFFFFFF;
    std::string text;
    std::shared_ptr<const Image> image;
};
struct List {
    std::string owner;
    uint32_t screen=0;
    size_t text_bytes=0,vertices=0;
    bool invalid=false;
    std::vector<Command> commands;
};
class Store {
    struct OwnedImage { std::string owner; std::shared_ptr<const Image> image; };
    std::mutex mutex;
    uint64_t next=1;
    // A process-specific epoch also distinguishes a full-state load in a fresh process.
    uint64_t generation=uint64_t(std::chrono::steady_clock::now().time_since_epoch().count())|1;
    std::map<Handle,List> pending;
    std::map<Handle,OwnedImage> images;
    std::map<std::string,std::vector<std::weak_ptr<const Image>>> allocations;
    std::map<std::pair<std::string,uint32_t>,std::shared_ptr<const List>> published;
    std::atomic<bool> visible{false};
    std::map<std::string,std::string> errors;
    Handle handle() {return next<=UINT32_MAX?uint32_t(next++):0;}
    static bool geometry(const Command& c) {
        return std::isfinite(c.x)&&std::isfinite(c.y)&&std::isfinite(c.w)&&std::isfinite(c.h)&&
               std::isfinite(c.size)&&std::abs(c.x)<=65536&&std::abs(c.y)<=65536&&
               std::abs(c.w)<=65536&&std::abs(c.h)<=65536&&c.size>=0&&c.size<=1024&&
               (c.kind==Command::Line||(c.w>=0&&c.h>=0))&&
               std::isfinite(c.thickness)&&c.thickness>0&&c.thickness<=1024&&
               std::isfinite(c.rotation)&&std::abs(c.rotation)<=65536&&
               std::isfinite(c.u0)&&std::isfinite(c.v0)&&std::isfinite(c.u1)&&std::isfinite(c.v1)&&
               c.u0>=0&&c.v0>=0&&c.u1<=1&&c.v1<=1&&c.u1>=c.u0&&c.v1>=c.v0&&
               uint32_t(c.anchor)<=uint32_t(Anchor::BottomRight)&&uint32_t(c.blend)<=uint32_t(Blend::Additive);
    }
    static bool utf8(const std::string& text) {
        for(size_t i=0;i<text.size();) {
            uint32_t cp=uint8_t(text[i++]);unsigned more=0;uint32_t minimum=0;
            if(cp<0x80){if(!cp)return false;continue;}
            if(cp>=0xC2&&cp<=0xDF){more=1;minimum=0x80;cp&=31;}
            else if(cp>=0xE0&&cp<=0xEF){more=2;minimum=0x800;cp&=15;}
            else if(cp>=0xF0&&cp<=0xF4){more=3;minimum=0x10000;cp&=7;}
            else return false;
            if(more>text.size()-i)return false;
            while(more--){uint8_t next=uint8_t(text[i++]);if((next&0xC0)!=0x80)return false;cp=(cp<<6)|(next&63);}
            if(cp<minimum||cp>0x10FFFF||(cp>=0xD800&&cp<=0xDFFF))return false;
        }
        return true;
    }
    bool append_locked(const std::string& owner,Handle handle,Command command) {
        auto it=pending.find(handle);
        if(it==pending.end()||it->second.owner!=owner)return false;
        auto& list=it->second;
        size_t vertices=command.kind==Command::Text?std::min(command.text.size(),kMaxText+1)*4:
                        (command.kind==Command::Circle||command.kind==Command::CircleOutline)?256:32;
        if(list.invalid||!geometry(command)||(command.kind==Command::Text&&!utf8(command.text))||list.commands.size()>=kMaxCommands||
           command.text.size()>kMaxText-list.text_bytes||vertices>kMaxVertices-list.vertices) {
            list.invalid=true;errors[owner]="HUD draw list dropped: invalid geometry or element/vertex/text limit";
            return false;
        }
        list.text_bytes+=command.text.size();list.vertices+=vertices;
        list.commands.push_back(std::move(command));return true;
    }
public:
    Handle begin(const std::string& owner,uint32_t screen) {
        std::lock_guard lock(mutex);
        if(owner.empty()||screen>2)return 0;
        size_t count=0;for(const auto& [h,list]:pending)count+=list.owner==owner;
        if(count>=kMaxLists){errors[owner]="HUD pending-list limit exceeded";return 0;}
        Handle h=handle();if(h)pending.emplace(h,List{owner,screen});return h;
    }
    bool fail(const std::string& owner,Handle h,const std::string& message) {
        std::lock_guard lock(mutex);auto it=pending.find(h);
        if(it!=pending.end()&&it->second.owner==owner){it->second.invalid=true;errors[owner]=message;}
        return false;
    }
    bool append(const std::string& owner,Handle h,Command command) {
        // Image references can only come from the ownership-checked picture() method.
        if(command.kind==Command::Picture||command.image||command.kind<Command::Rect||command.kind>Command::Line)
            return fail(owner,h,"HUD draw list dropped: invalid primitive");
        std::lock_guard lock(mutex);return append_locked(owner,h,std::move(command));
    }
    Handle create_image(const std::string& owner,uint32_t width,uint32_t height,uint32_t stride,
                        const uint8_t* source,size_t bytes) {
        if(owner.empty()||!source||!width||!height||width>2048||height>2048||uint64_t(width)*4>stride)return 0;
        const uint64_t required=uint64_t(height-1)*stride+uint64_t(width)*4;
        const size_t size=size_t(width)*height*4;
        if(required>bytes||size>kMaxPixels)return 0;
        std::lock_guard lock(mutex);
        auto& live=allocations[owner];size_t used=0,count=0;
        std::erase_if(live,[](const auto& weak){return weak.expired();});
        for(const auto& weak:live)if(auto image=weak.lock()){used+=image->rgba.size();++count;}
        if(count>=kMaxImages||used>kMaxPixels-size)return 0;
        Handle h=handle();if(!h)return 0;
        auto image=std::make_shared<Image>();image->width=width;image->height=height;image->rgba.resize(size);
        for(uint32_t y=0;y<height;++y)std::copy_n(source+size_t(y)*stride,size_t(width)*4,image->rgba.data()+size_t(y)*width*4);
        images.emplace(h,OwnedImage{owner,image});live.push_back(image);return h;
    }
    bool picture(const std::string& owner,Handle list,Handle image,Command command) {
        std::lock_guard lock(mutex);auto it=images.find(image);
        if(it==images.end()||it->second.owner!=owner) {
            auto pending_list=pending.find(list);
            if(pending_list!=pending.end()&&pending_list->second.owner==owner) {
                pending_list->second.invalid=true;errors[owner]="HUD draw list dropped: invalid texture handle";
            }
            return false;
        }
        command.kind=Command::Picture;command.image=it->second.image;
        return append_locked(owner,list,std::move(command));
    }
    bool release(const std::string& owner,Handle h) {
        std::lock_guard lock(mutex);auto it=images.find(h);
        if(it==images.end()||it->second.owner!=owner)return false;
        images.erase(it);return true;
    }
    bool commit(const std::string& owner,Handle h) {
        std::lock_guard lock(mutex);auto it=pending.find(h);
        if(it==pending.end()||it->second.owner!=owner)return false;
        auto key=std::make_pair(owner,it->second.screen);
        bool valid=!it->second.invalid;
        if(!valid||it->second.commands.empty())published.erase(key);
        else published[key]=std::make_shared<const List>(std::move(it->second));
        visible.store(!published.empty(),std::memory_order_release);
        pending.erase(it);return valid;
    }
    bool cancel(const std::string& owner,Handle h) {
        std::lock_guard lock(mutex);auto it=pending.find(h);
        if(it==pending.end()||it->second.owner!=owner)return false;
        pending.erase(it);return true;
    }
    std::vector<std::shared_ptr<const List>> snapshot(uint32_t screen) {
        std::lock_guard lock(mutex);std::vector<std::shared_ptr<const List>> out;
        for(const auto& [key,list]:published)if(key.second==screen||key.second==2)out.push_back(list);
        return out;
    }
    void note(const std::string& owner,const std::string& message) {std::lock_guard lock(mutex);errors[owner]=message;}
    std::string error(const std::string& owner) {std::lock_guard lock(mutex);auto it=errors.find(owner);return it==errors.end()?std::string{}:it->second;}
    void drop(const std::string& owner) {
        std::lock_guard lock(mutex);
        std::erase_if(pending,[&](const auto& item){return item.second.owner==owner;});
        std::erase_if(published,[&](const auto& item){return item.first.first==owner;});
        visible.store(!published.empty(),std::memory_order_release);
    }
    uint64_t state_generation() {std::lock_guard lock(mutex);return generation;}
    bool active() const {return visible.load(std::memory_order_acquire);}
    void reset() {
        std::lock_guard lock(mutex);pending.clear();images.clear();published.clear();
        visible.store(false,std::memory_order_release);
        // Old render snapshots may still own pixels: keep their weak quota records.
        if(++generation==0)generation=1;
    }
};
inline Store& store() {static Store value;return value;}
} // namespace guestmods::hud
