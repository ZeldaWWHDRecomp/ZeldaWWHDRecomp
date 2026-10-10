#include "mods/guest_hud.h"
#include <cassert>
#include <thread>
using namespace guestmods::hud;
int main() {
    Store store;uint8_t source[]={255,0,0,255,99,99,99,99,0,255,0,255};
    auto image=store.create_image("a",1,2,8,source,sizeof source);assert(image);
    assert(!store.create_image("a",1,2,8,source,sizeof source-1));
    assert(!store.create_image("a",2049,1,8196,source,sizeof source));
    auto list=store.begin("a",0);assert(list&&!store.begin("a",3));
    Command command;command.w=100;command.h=100;
    assert(!store.picture("b",list,image,command));
    assert(store.picture("a",list,image,command));
    assert(store.snapshot(0).empty()); // transactional publication
    assert(store.commit("a",list)&&!store.commit("a",list));
    auto frames=store.snapshot(0);assert(frames.size()==1&&store.snapshot(1).empty());
    auto retained=frames[0]->commands[0].image;source[0]=0;
    assert(retained->rgba.size()==8&&retained->rgba[0]==255&&retained->rgba[5]==255);
    assert(!store.release("b",image)&&store.release("a",image));
    assert(retained->rgba[0]==255); // released images live through in-flight frames
    auto bad=store.begin("a",1);command.x=std::numeric_limits<float>::quiet_NaN();
    assert(!store.append("a",bad,command));command.x=0;
    assert(!store.commit("a",bad)&&!store.error("a").empty());
    bad=store.begin("a",1);
    command.kind=Command::Text;command.text.assign(kMaxVertices/4,'x');assert(store.append("a",bad,command));
    assert(!store.append("a",bad,command));assert(!store.cancel("b",bad)&&store.cancel("a",bad));
    std::vector<Handle> abandoned;
    for(unsigned i=0;i<kMaxLists;++i)abandoned.push_back(store.begin("a",0));
    assert(!store.begin("a",0));for(auto h:abandoned)assert(store.cancel("a",h));
    auto old_generation=store.state_generation();auto stale=store.begin("a",0);store.reset();
    assert(store.state_generation()!=old_generation&&store.snapshot(0).empty());
    assert(!store.commit("a",stale)&&store.begin("a",0)!=stale);
    // Both-screen publication and immutable snapshots survive replacement and reset.
    command=Command{};auto both=store.begin("both",2);
    assert(store.append("both",both,command)&&store.commit("both",both));
    assert(store.snapshot(0).size()==1&&store.snapshot(1).size()==1);
    auto frozen=store.snapshot(1);store.drop("both");assert(!store.active());
    assert(frozen[0]->commands.size()==1);
    // Each primitive accepts finite geometry, signed line deltas and bounded UVs.
    auto primitives=store.begin("shapes",0);
    for(auto kind:{Command::Rect,Command::RectOutline,Command::Circle,Command::CircleOutline,Command::Line,Command::Text}) {
        command=Command{};command.kind=kind;command.w=kind==Command::Line?-10:10;
        command.h=10;command.text="Hearts: ♥";command.anchor=Anchor::TopRight;command.blend=Blend::Additive;
        assert(store.append("shapes",primitives,command));
    }
    assert(store.commit("shapes",primitives));store.drop("shapes");
    // Clips are transactional, owner-scoped and balanced within one list.
    auto clipped=store.begin("clip",0);command=Command{};command.kind=Command::ClipPush;command.w=20;command.h=30;
    assert(!store.append("other",clipped,command));
    for(unsigned i=0;i<kMaxClipDepth;++i)assert(store.append("clip",clipped,command));
    assert(!store.append("clip",clipped,command)&&!store.commit("clip",clipped));
    assert(store.snapshot(0).empty());
    clipped=store.begin("clip",0);assert(store.append("clip",clipped,command));
    assert(!store.commit("clip",clipped)); // no clip may escape its callback
    clipped=store.begin("clip",0);command.kind=Command::ClipPop;
    assert(!store.append("clip",clipped,command)&&!store.commit("clip",clipped));
    clipped=store.begin("clip",0);command.kind=Command::ClipPush;
    for(unsigned i=0;i<kMaxClipDepth;++i)assert(store.append("clip",clipped,command));
    command.kind=Command::ClipPop;
    assert(!store.append("other",clipped,command));
    for(unsigned i=0;i<kMaxClipDepth;++i)assert(store.append("clip",clipped,command));
    assert(store.commit("clip",clipped));store.drop("clip");
    clipped=store.begin("clip",0);command.kind=Command::ClipPush;command.w=-1;
    assert(!store.append("clip",clipped,command)&&!store.commit("clip",clipped));
    // Released textures remain charged while old renderer snapshots retain them.
    Store quota;uint8_t pixel[4]{};std::vector<Handle> handles;
    for(unsigned i=0;i<kMaxImages;++i){auto h=quota.create_image("q",1,1,4,pixel,4);assert(h);handles.push_back(h);}
    assert(!quota.create_image("q",1,1,4,pixel,4));
    auto qlist=quota.begin("q",0);command=Command{};
    assert(quota.picture("q",qlist,handles[0],command)&&quota.commit("q",qlist));
    auto old=quota.snapshot(0);assert(quota.release("q",handles[0]));quota.drop("q");
    assert(!quota.create_image("q",1,1,4,pixel,4));old.clear();
    assert(quota.create_image("q",1,1,4,pixel,4));
    auto capped=store.begin("caps",0);command=Command{};
    for(unsigned i=0;i<kMaxCommands;++i)assert(store.append("caps",capped,command));
    assert(!store.append("caps",capped,command)&&!store.commit("caps",capped));
    assert(store.snapshot(0).empty());
    auto invalid_utf=store.begin("utf",0);command.kind=Command::Text;command.text="\xED\xA0\x80";
    assert(!store.append("utf",invalid_utf,command)&&!store.commit("utf",invalid_utf));
    for(float aspect:{16.f/9,21.f/9,32.f/9}) {
        float width=720*aspect;
        auto left=anchored(20,30,Anchor::TopLeft,width,720,1280,720);
        auto right=anchored(1260,30,Anchor::TopRight,width,720,1280,720);
        auto center=anchored(640,360,Anchor::Center,width,720,1280,720);
        assert(std::abs(left.x-20)<.001f&&std::abs(right.x-(width-20))<.001f);
        assert(std::abs(center.x-width/2)<.001f&&center.y==360);
    }
    auto drc=anchored(834,460,Anchor::BottomRight,1708,960,854,480);
    assert(drc.x==1668&&drc.y==920);
    // Publication and rendering may occur on separate threads.
    std::thread reader([&]{for(int i=0;i<1000;++i)store.snapshot(0);});
    for(int i=0;i<1000;++i){auto h=store.begin("a",0);assert(h);assert(store.commit("a",h));}
    reader.join();
}
