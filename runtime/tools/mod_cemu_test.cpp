#include "mods/cemu_pack.h"
#include "mods/shader_interface.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using namespace mods;
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(int argc,char** argv) {
    if(argc==2||argc==3) {
        auto pack=cemu::parse(argv[1]);std::cout<<pack.name<<": "<<pack.textures.size()<<" rules, "<<pack.shaders.size()<<" shaders, "<<pack.presets.size()<<" presets\n";
        for(size_t i=0;i<pack.categories.size();i++)for(const auto& preset:pack.presets)if(preset.category==pack.categories[i]){
            json::Value config;config["preset-"+std::to_string(i)]=preset.name;
            cemu::validate({{"public-pack",pack,config}});
        }
        if(argc==3){fs::path output=argv[2];fs::create_directories(output);cemu::set_vulkan(true);cemu::activate({{"public-pack",pack,{}}});for(size_t i=0;i<pack.shaders.size();i++){auto& shader=pack.shaders[i];std::ofstream(output/(std::to_string(i)+(shader.vertex?".vert":".frag")))<<cemu::shader_source(shader.base,shader.aux,shader.vertex);}}
        return 0;
    }
    assert(cemu::expression("max(2, $width / 2) + floor(1.9)",{{"$width",8}})==5);
    assert(cemu::expression("0x80e",{})==2062);
    rejects([]{cemu::expression("1/0",{});});rejects([]{cemu::expression("$missing",{});});
    auto root=fs::temp_directory_path()/"wwhd-cemu-unit-tests";fs::create_directories(root);
    auto write=[&](const std::string& text){std::ofstream(root/"rules.txt")<<text;};
    std::string definition="[Definition]\nname = Test\ntitleIds = 0005000010143500\nversion = 4\n";
    std::string presets="[Default]\n$scale = 1\n[Preset]\nname = Small\ncategory = Resolution\n$scale = 1\n[Preset]\nname = Large\ncategory = Resolution\n$scale = 2\n";
    std::string texture="[TextureRedefine]\nwidth = 1920\nheight = 1080\nformats = 0x80e\ntileModes = 0, 4\noverwriteWidth = 1920 * $scale\noverwriteHeight = 1080 * $scale\n";
    write(definition+presets+texture);auto pack=cemu::parse(root);
    assert(cemu::options(pack).array.size()==1);
    json::Value config;config["preset-0"]="Large";
    cemu::validate({{"a",pack,config}});
    rejects([&]{cemu::validate({{"a",pack,config},{"b",pack,{}}});});
    write(definition+"[TextureRedefine]\noverwriteFormat = 0x80e\n");rejects([&]{cemu::parse(root);});
    write(definition+"[Patch]\nmoduleMatches = 0x123\n");rejects([&]{cemu::parse(root);});
    write("[Definition]\nname=Wrong\ntitleIds=10005000010143500\nversion=4\n"+presets+texture);rejects([&]{cemu::parse(root);});
    write(definition+presets+texture);
    std::ofstream(root/"patches.txt")<<"[Bad]\n0x1004AAF0 = .float 2\n";rejects([&]{cemu::parse(root);});fs::remove(root/"patches.txt");
    auto shader=root/"0000000000000001_0000000000000002_ps.txt";
    std::ofstream(shader)<<"#version 420\n// $missing preserved in comments\nvoid main(){ float x=$scale; }\n";
    auto shaderPack=cemu::parse(root);
    std::ofstream(root/"texture.dds")<<"synthetic unsupported resource";rejects([&]{cemu::parse(root);});fs::remove(root/"texture.dds");
    cemu::activate({{"shader",shaderPack,config}});uint32_t w=0,h=0;
    cemu::set_vulkan(false);assert(!cemu::texture_extent(1920,1080,0x80e,1,0,w,h));assert(cemu::shader_source(1,2,false).empty());
    cemu::set_vulkan(true);assert(cemu::texture_extent(1920,1080,0x80e,1,0,w,h)&&w==3840&&h==2160);
    assert(!cemu::texture_extent(1920,1080,0x19,1,0,w,h));
    auto expanded=cemu::shader_source(1,2,false);assert(expanded.find("#define VULKAN 1")!=std::string::npos&&expanded.find("float x=(2.0)")!=std::string::npos);
    assert(cemu::shader_source(1,3,false).empty());
    rejects([&]{cemu::activate({});});
    // Synthetic SPIR-V scalar buffer: detect descriptor rebinding and offset drift.
    std::vector<uint32_t> spirv={0x07230203,0x10000,0,10,0,
        (3u<<16)|22,1,32, (3u<<16)|30,2,1,
        (3u<<16)|71,2,2, (5u<<16)|72,2,0,35,0,
        (4u<<16)|32,3,2,2, (4u<<16)|59,3,4,2,
        (4u<<16)|71,4,34,1, (4u<<16)|71,4,33,0};
    std::string error;assert(cemu::compatible_shader_interface(spirv,spirv,error));
    auto changed=spirv;changed.back()=7;assert(!cemu::compatible_shader_interface(spirv,changed,error));
    changed=spirv;changed[18]=16;assert(!cemu::compatible_shader_interface(spirv,changed,error));
    // Pixel inputs without a vertex shader output become the translation's constants.
    const char* unfed[32]={};unfed[1]="vec4(0.0, 0.0, 0.0, 1.0)";unfed[12]="vec4(1.0, 1.0, 1.0, 1.0)";
    auto pixel=cemu::const_pixel_inputs("layout(location = 0) in vec4 passParameterSem0;\nlayout(location=1) noperspective in vec4 passParameterSem3;\n"
        "layout( location = 12 ) flat in vec4 tint;\nlayout(location = 12) out vec4 color;\nlayout(location = 1, component = 0) in vec4 other;\n// layout(location = 1) in vec2 uv;\n",unfed);
    assert(pixel.find("layout(location = 0) in vec4 passParameterSem0;")!=std::string::npos);
    assert(pixel.find("const vec4 passParameterSem3 = vec4(0.0, 0.0, 0.0, 1.0);")!=std::string::npos);
    assert(pixel.find("const vec4 tint = vec4(1.0, 1.0, 1.0, 1.0);")!=std::string::npos);
    assert(pixel.find("layout(location = 12) out vec4 color;")!=std::string::npos);
    assert(pixel.find("layout(location = 1, component = 0) in vec4 other;")!=std::string::npos);
    assert(pixel.find("layout(location = 1) in vec2 uv;")!=std::string::npos);
    fs::remove_all(root);std::cout<<"Cemu rules, presets, conflicts, backend guard, shader layouts and unfed pixel inputs passed\n";
}
