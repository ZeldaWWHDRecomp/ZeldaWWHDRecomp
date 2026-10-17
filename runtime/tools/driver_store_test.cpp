#include "gfx/vulkan/driver_store.h"
#include <cassert>
#include <fstream>
int main(int argc,char** argv) {
    assert(argc==4);namespace fs=std::filesystem;using gfxvk::drivers::Store;
    auto root=fs::path(argv[1]);fs::remove_all(root);
    Store first(root);assert(!first.start());auto id=first.install(argv[2],33);assert(first.list().size()==1);
    assert(first.read(root/id).name=="Synthetic Turnip");first.select(id);
    Store probe(root);assert(!probe.start());
    for(int i=0;i<119;++i)probe.rendered_frame();assert(fs::exists(root/"probing"));
    Store recovery(root);assert(recovery.start()&&recovery.selected().empty());assert(!fs::exists(root/"probing"));
    recovery.select(id);Store success(root);assert(!success.start());
    for(int i=0;i<120;++i)success.rendered_frame();assert(!fs::exists(root/"probing"));
    success.note_device_lost("Synthetic mature custom driver",true);
    Store mature(root);auto mature_loss=mature.take_device_loss();assert(mature_loss&&mature_loss->custom&&mature.selected()==id);
    assert(!fs::exists(root/"probing"));assert(!mature.take_device_loss());
    Store restart(root);assert(!restart.start()&&restart.selected()==id);restart.failed();assert(restart.selected().empty());
    bool refused=false;try{first.install(argv[3],33);}catch(...){refused=true;}assert(refused);
    refused=false;try{first.install(argv[2],1);}catch(...){refused=true;}assert(refused);
    refused=false;try{first.select("../../outside");}catch(...){refused=true;}assert(refused);
    auto cache=first.cache(id);fs::create_directories(cache);std::ofstream(cache/"pipeline.bin")<<"synthetic-cache";
    first.remove(id);assert(!fs::exists(cache)&&first.list().empty()&&first.selected().empty());
    // Mature custom-driver failures and system-driver failures both survive a restart.
    first.note_device_lost("Synthetic Vulkan 1.2 driver",false);
    Store next(root);auto lost=next.take_device_loss();assert(lost&&lost->driver=="Synthetic Vulkan 1.2 driver"&&!lost->custom);
    assert(!next.take_device_loss());assert(next.selected().empty());
    next.note_device_lost(std::string(600,'x')+"\n",true);Store later(root);lost=later.take_device_loss();assert(lost&&lost->custom&&lost->driver==std::string(512,'x'));
    later.note_device_lost("\n\r\t",false);lost=later.take_device_loss();assert(lost&&lost->driver=="Unknown Vulkan driver");
    std::ofstream(root/"device-lost.json")<<"{\"schema\":1,\"driver\":\"x\",\"custom\":false,\"extra\":\""<<std::string(3000,'x')<<"\"}";
    refused=false;try{later.take_device_loss();}catch(...){refused=true;}assert(refused);
    fs::remove_all(root);
}
