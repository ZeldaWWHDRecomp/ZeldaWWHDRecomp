#include "mods/guest_png.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <functional>
#include <zlib.h>
using namespace guestmods::hud;
namespace {
void be(std::vector<uint8_t>& out,uint32_t n) {for(int shift:{24,16,8,0})out.push_back(n>>shift);}
void chunk(std::vector<uint8_t>& out,const char* type,const std::vector<uint8_t>& data) {
    be(out,uint32_t(data.size()));size_t start=out.size();
    out.insert(out.end(),type,type+4);out.insert(out.end(),data.begin(),data.end());
    be(out,uint32_t(crc32(0,out.data()+start,uInt(data.size()+4))));
}
std::vector<uint8_t> png(uint32_t width=1) {
    std::vector<uint8_t> out{137,80,78,71,13,10,26,10},header;
    be(header,width);be(header,1);header.insert(header.end(),{8,6,0,0,0});chunk(out,"IHDR",header);
    const uint8_t raw[]{0,21,42,63,127};uLongf size=compressBound(sizeof raw);
    std::vector<uint8_t> zipped(size);assert(compress(zipped.data(),&size,raw,sizeof raw)==Z_OK);
    zipped.resize(size);chunk(out,"IDAT",zipped);chunk(out,"IEND",{});return out;
}
void rejects(const std::function<void()>& fn) {bool failed=false;try{fn();}catch(const std::exception&){failed=true;}assert(failed);}
}
int main() {
    auto bytes=png();auto image=decode_png(bytes);
    assert(image.width==1&&image.height==1&&image.rgba==std::vector<uint8_t>({21,42,63,127}));
    rejects([]{decode_png({});});rejects([]{decode_png(png(2049));});
    rejects([]{decode_png(png(0));});
    auto truncated=bytes;truncated.resize(40);rejects([&]{decode_png(truncated);});
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/("wwhd-png-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"assets");
    {std::ofstream file(root/"assets/pixel.png",std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    assert(load_png(root,"assets/pixel.png").rgba==image.rgba);
    rejects([&]{load_png(root,"../pixel.png");});rejects([&]{load_png(root,"/pixel.png");});
    rejects([&]{load_png(root,"missing.png");});
    std::error_code ec;fs::create_directory_symlink(root/"assets",root/"linked",ec);
    if(!ec)rejects([&]{load_png(root,"linked/pixel.png");});
    fs::remove_all(root);
}
