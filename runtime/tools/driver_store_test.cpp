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
    Store restart(root);assert(!restart.start()&&restart.selected()==id);restart.failed();assert(restart.selected().empty());
    bool refused=false;try{first.install(argv[3],33);}catch(...){refused=true;}assert(refused);
    refused=false;try{first.install(argv[2],1);}catch(...){refused=true;}assert(refused);
    refused=false;try{first.select("../../outside");}catch(...){refused=true;}assert(refused);
    auto cache=first.cache(id);fs::create_directories(cache);std::ofstream(cache/"pipeline.bin")<<"synthetic-cache";
    first.remove(id);assert(!fs::exists(cache)&&first.list().empty()&&first.selected().empty());
    fs::remove_all(root);
}
