#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#include "mods/static_model_validation.h"
#include "static_model_fixture.h"
using namespace static_model_fixture;
int main(){static_models::ModelInfo info;std::string why;auto doc=document();auto bin=binary();auto good=glb(doc,bin);assert(static_models::validate_glb(good,info,why));assert(info.vertex_count==3&&info.triangle_count==1&&info.texture_width==64&&info.texture_height==8);
    auto bad=[&](const Value& d,const std::vector<uint8_t>& b){info.vertex_count=99;assert(!static_models::validate_glb(glb(d,b),info,why));assert(info.vertex_count==0&&!why.empty());};
    for(size_t n=0;n<good.size();++n){auto truncated=std::span<const uint8_t>(good).first(n);assert(!static_models::validate_glb(truncated,info,why));}
    doc=document();doc["accessors"].array[0]["count"]=4097;bad(doc,bin);
    doc=document();doc["accessors"].array[3]["count"]=8192*3+3;bad(doc,bin);
    doc=document();doc["accessors"].array[0]["byteOffset"]=35;bad(doc,bin);
    doc=document();doc["accessors"].array[0]["sparse"]=Value(true);bad(doc,bin);
    doc=document();doc["buffers"].array[0]["uri"]="outside.bin";bad(doc,bin);
    doc=document();doc["animations"]=mods::json::parse("[{}]");bad(doc,bin);
    doc=document();doc["nodes"].array[0]["children"]=mods::json::parse("[0]");bad(doc,bin);
    doc=document();doc["nodes"].array[0]["mesh"]=1;bad(doc,bin);
    doc=document();doc["nodes"].array[0]["translation"]=mods::json::parse("[1,2,3]");assert(static_models::validate_glb(glb(doc,bin),info,why));
    doc["nodes"].array[0]["translation"]=mods::json::parse("[1,2]");bad(doc,bin);
    doc=document();auto corrupt=bin;corrupt[100]=3;bad(doc,corrupt);
    corrupt=bin;corrupt[2]=0x80;corrupt[3]=0x7f;bad(doc,corrupt);
    corrupt=bin;corrupt[104+18]=4;bad(doc,corrupt);
    corrupt=bin;corrupt[104+25]=3;bad(doc,corrupt);
    doc=document();doc["images"].array.push_back(doc["images"].array[0]);bad(doc,bin);
    doc=document();doc["materials"].array[0]["normalTexture"]=mods::json::parse("{\"index\":0}");bad(doc,bin);
    doc=document();doc.object.erase("scene");assert(static_models::validate_glb(glb(doc,bin),info,why));
    doc=document();doc["accessors"].array[0]["normalized"]=false;assert(static_models::validate_glb(glb(doc,bin),info,why));
    doc=document();doc["materials"].array[0]["alphaMode"]="BLEND";bad(doc,bin);
    doc=document();doc["materials"].array[0]["doubleSided"]=true;assert(static_models::validate_glb(glb(doc,bin),info,why)&&info.double_sided_count==1);
    doc=document();doc["materials"].array[0]["doubleSided"]=1;bad(doc,bin);
    doc=document();doc["materials"].array[0]["pbrMetallicRoughness"]["baseColorFactor"]=mods::json::parse("[1,0,1,1]");bad(doc,bin);
    doc=document();doc["materials"].array[0]["pbrMetallicRoughness"]["metallicRoughnessTexture"]=mods::json::parse("{\"index\":0}");bad(doc,bin);
    doc=document();doc["asset"]["extensions"]=mods::json::parse("{\"unsafe\":{}}");bad(doc,bin);
    doc=document();doc["nodes"].array[0]["scale"]=mods::json::parse("[1,0,1]");bad(doc,bin);
    doc=document();doc["nodes"].array[0]["rotation"]=mods::json::parse("[0,0,0,2]");bad(doc,bin);
    doc=document();doc["nodes"].array[0]["translation"]=mods::json::parse("[10001,0,0]");bad(doc,bin);
    doc=document();doc["nodes"].array[0]["scale"]=mods::json::parse("[-1,1,1]");assert(static_models::validate_glb(glb(doc,bin),info,why));
    doc=document();doc["nodes"].array[0]=mods::json::parse("{\"translation\":[1,2,3],\"children\":[1]}");doc["nodes"].array.push_back(mods::json::parse("{\"mesh\":0}"));assert(static_models::validate_glb(glb(doc,bin),info,why));
    doc=document();corrupt=bin;corrupt[104+29]^=1;bad(doc,corrupt); // IHDR CRC
    doc=document();corrupt=bin;corrupt[104+8]=0x7f;bad(doc,corrupt); // oversized chunk
    doc=document();doc["bufferViews"].array[4]["byteLength"]=int(document().get("bufferViews").array[4].get("byteLength").number)-12;bad(doc,bin); // missing IEND
    doc=document();auto positioned=binary();const float positions[9]={-1,2,3,4,-5,6,7,8,-9};for(size_t k=0;k<9;++k){uint32_t bits;std::memcpy(&bits,&positions[k],4);for(size_t c=0;c<4;++c)positioned[k*4+c]=uint8_t(bits>>(8*c));}
    assert(static_models::validate_glb(glb(doc,positioned),info,why));assert((info.minimum==std::array<float,3>{-100,-500,-900})&&(info.maximum==std::array<float,3>{700,800,600}));
    doc["nodes"].array[0]["translation"]=mods::json::parse("[1,2,3]");doc["nodes"].array[0]["scale"]=mods::json::parse("[2,3,4]");assert(static_models::validate_glb(glb(doc,positioned),info,why));assert((info.minimum==std::array<float,3>{-100,-1300,-3300})&&(info.maximum==std::array<float,3>{1500,2600,2700}));
    doc=cube_document();const auto cube=cube_binary();assert(static_models::validate_glb(glb(doc,cube),info,why)&&info.vertex_count==8&&info.triangle_count==12);assert((info.minimum==std::array<float,3>{-50,-50,-50})&&(info.maximum==std::array<float,3>{50,50,50}));
    doc["nodes"].array[0]["translation"]=mods::json::parse("[1,2,3]");doc["nodes"].array[0]["scale"]=mods::json::parse("[2,3,4]");assert(static_models::validate_glb(glb(doc,cube),info,why));assert((info.minimum==std::array<float,3>{0,50,100})&&(info.maximum==std::array<float,3>{200,350,500}));
    // Original collision-only mesh: no normals/UV/texture, separate material,
    // nested mirrored transform. Rendering bounds and PNG offset stay authored.
    auto with_collision=[](){auto d=cube_document();auto mesh=d["meshes"].array[0];mesh["name"]="island_col";mesh["primitives"].array[0]["attributes"]=mods::json::parse("{\"POSITION\":0}");mesh["primitives"].array[0]["material"]=1;d["meshes"].array.push_back(mesh);d["materials"].array.push_back(mods::json::parse("{\"name\":\"col_grass\"}"));d["nodes"].array.push_back(mods::json::parse("{\"translation\":[2,1,0],\"children\":[2]}"));d["nodes"].array.push_back(mods::json::parse("{\"mesh\":1,\"scale\":[-1,2,3]}"));d["scenes"].array[0]["nodes"].array.push_back(1);return d;};
    doc=with_collision();auto collision_glb=glb(doc,cube);assert(static_models::validate_glb(collision_glb,info,why));assert(info.vertex_count==8&&info.triangle_count==12);assert((info.minimum==std::array<float,3>{-50,-50,-50}));assert(collision_glb[info.image_offset]==137&&collision_glb[info.image_offset+1]==80);
    auto second=doc["meshes"].array[1]["primitives"].array[0];second["material"]=2;doc["materials"].array.push_back(mods::json::parse("{\"name\":\"col_wall\"}"));doc["meshes"].array[1]["primitives"].array.push_back(second);assert(static_models::validate_glb(glb(doc,cube),info,why));
    doc=with_collision();doc["materials"].array[1]["name"]="col_objects";assert(static_models::validate_glb(glb(doc,cube),info,why));
    doc=with_collision();doc["materials"].array[1]["name"]="col_climb";assert(static_models::validate_glb(glb(doc,cube),info,why));
    doc=with_collision();doc["materials"].array[1]["name"]="col_hookshot";assert(static_models::validate_glb(glb(doc,cube),info,why));
    doc=with_collision();doc["materials"].array[1]["name"]="col_unknown";bad(doc,cube);
    doc=with_collision();doc["nodes"].array[2]["scale"]=mods::json::parse("[0,1,1]");bad(doc,cube);
    // a body's size: one sea square (100000 units = 1 km) along each axis and every coordinate within +/-100000
    static_assert(static_models::kMaxCollisionExtent==100000);
    doc=with_collision();doc["nodes"].array[2]["scale"]=mods::json::parse("[1000,1,1]");assert(static_models::validate_glb(glb(doc,cube),info,why));  // exactly 1 km wide
    doc=with_collision();doc["nodes"].array[2]["scale"]=mods::json::parse("[1000.001,1,1]");bad(doc,cube);  // 0.1 unit wider
    doc=with_collision();doc["nodes"].array[2]["scale"]=mods::json::parse("[1,1,1000.001]");bad(doc,cube);
    doc=with_collision();doc["nodes"].array[1]["translation"]=mods::json::parse("[201,0,0]");assert(static_models::validate_glb(glb(doc,cube),info,why));  // past the old 200 m
    doc=with_collision();doc["nodes"].array[1]["translation"]=mods::json::parse("[997.5,0,0]");assert(static_models::validate_glb(glb(doc,cube),info,why));  // up to 99800 units
    doc=with_collision();doc["nodes"].array[1]["translation"]=mods::json::parse("[999.501,0,0]");bad(doc,cube);  // a coordinate past 100000
    doc=with_collision();doc["nodes"].array[1]["translation"]=mods::json::parse("[0,0,-999]");bad(doc,cube);
    doc=with_collision();doc["scenes"].array[0]["nodes"].array.pop_back();bad(doc,cube);
    doc=with_collision();doc["meshes"].array[1]["primitives"].array[0]["mode"]=1;bad(doc,cube);
    doc=cube_document();assert(static_models::validate_glb(glb(doc,cube),info,why,true));doc["nodes"].array[0]["translation"]=mods::json::parse("[201,0,0]");assert(static_models::validate_glb(glb(doc,cube),info,why,true));doc["nodes"].array[0]["translation"]=mods::json::parse("[1000,0,0]");assert(!static_models::validate_glb(glb(doc,cube),info,why,true)&&info.vertex_count==0);
    doc=with_collision();auto flat=cube;for(size_t n=0;n<96;++n)flat[n]=0;bad(doc,flat);
    // 11 copies of 64 cube primitives: 8448 collision triangles, past the 8192 of a body
    doc=with_collision();auto many=doc["meshes"].array[1]["primitives"].array[0];doc["meshes"].array[1]["primitives"].array.assign(64,many);doc["nodes"].array.push_back(mods::json::parse("{\"mesh\":1}"));doc["scenes"].array[0]["nodes"].array.push_back(3);for(unsigned n=0;n<9;++n){doc["nodes"].array.push_back(mods::json::parse("{\"mesh\":1}"));doc["scenes"].array[0]["nodes"].array.push_back(int(4+n));}bad(doc,cube);
    // Several materials: one embedded PNG per material, several primitives (tools/story_mods/multi_material.py).
    auto two=[](){auto d=document();auto p=d["meshes"].array[0]["primitives"].array[0];p["material"]=1;d["meshes"].array[0]["primitives"].array.push_back(p);d["materials"].array.push_back(mods::json::parse(R"({"name":"leaves","alphaMode":"MASK","alphaCutoff":0.4,"doubleSided":true,"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})"));return d;};
    doc=two();assert(static_models::validate_glb(glb(doc,bin),info,why));assert(info.material_count==2&&info.cutout_count==1&&info.double_sided_count==1&&info.images.size()==1&&info.vertex_count==6&&info.triangle_count==2&&info.texture_bytes==64*8*4);
    doc=two();doc["images"].array.push_back(doc["images"].array[0]);doc["textures"].array.push_back(mods::json::parse(R"({"source":1})"));doc["materials"].array[1]["pbrMetallicRoughness"]["baseColorTexture"]["index"]=1;
    {auto data=glb(doc,bin);assert(static_models::validate_glb(data,info,why)&&info.images.size()==2&&info.texture_bytes==2*64*8*4);for(const auto& image:info.images)assert(data[image.offset]==137&&data[image.offset+1]=='P'&&image.width==64&&image.height==8);}
    auto refused=[&](const Value& d,const char* words){info.vertex_count=99;assert(!static_models::validate_glb(glb(d,bin),info,why));assert(info.vertex_count==0&&why.find(words)!=std::string::npos);};
    doc=two();doc["materials"].array[1]["alphaMode"]="BLEND";doc["materials"].array[1].object.erase("alphaCutoff");refused(doc,"blended transparency");
    doc=two();doc["materials"].array[0]["alphaCutoff"]=0.5;refused(doc,"alphaCutoff needs alphaMode MASK");
    doc=two();doc["materials"].array[1]["alphaCutoff"]=1;refused(doc,"between 0 and 1");
    doc=two();doc["meshes"].array[0]["primitives"].array.pop_back();refused(doc,"every material must be used");
    doc=two();doc["meshes"].array[0]["primitives"].array[1]["material"]=2;refused(doc,"model's materials");
    doc=two();doc["images"].array.push_back(doc["images"].array[0]);refused(doc,"every embedded image");
    doc=two();doc["textures"].array.push_back(doc["textures"].array[0]);refused(doc,"every texture");
    // double-sided faces count twice: 2100 vertices fit once, mirrored they do not
    {auto d=document();d["accessors"].array[0]["count"]=2100;d["accessors"].array[1]["count"]=2100;d["accessors"].array[2]["count"]=2100;const int lens[]={2100*12,2100*12,2100*8,6,101};int off=0;for(int n=0;n<5;++n){d["bufferViews"].array[n]["byteOffset"]=off;d["bufferViews"].array[n]["byteLength"]=lens[n];off+=(lens[n]+3)/4*4;}d["buffers"].array[0]["byteLength"]=off-3;std::vector<uint8_t> big(off);const auto small=binary();for(int k=0;k<2100;++k){big[2100*12+k*12+6]=0x80;big[2100*12+k*12+7]=0x3f;}big[2100*32+2]=1;big[2100*32+4]=2;std::copy(small.begin()+104,small.begin()+205,big.begin()+2100*32+8);
     assert(static_models::validate_glb(glb(d,big),info,why));d["materials"].array[0]["doubleSided"]=true;assert(!static_models::validate_glb(glb(d,big),info,why)&&why.find("double-sided")!=std::string::npos);}
    doc=two();{auto p=doc["meshes"].array[0]["primitives"].array[0];doc["meshes"].array[0]["primitives"].array.assign(33,p);doc["meshes"].array[0]["primitives"].array[1]["material"]=1;}refused(doc,"more than 32 primitives");
    doc=two();{auto p=doc["meshes"].array[0]["primitives"].array[0];auto m=doc["materials"].array[0];doc["materials"].array.assign(17,m);doc["meshes"].array[0]["primitives"].array.clear();for(int i=0;i<17;++i){p["material"]=i;doc["meshes"].array[0]["primitives"].array.push_back(p);}}refused(doc,"at most 16");
    doc=two();{auto p=doc["meshes"].array[0]["primitives"].array[0];auto m=doc["materials"].array[0];doc["materials"].array.assign(16,m);doc["meshes"].array[0]["primitives"].array.clear();for(int i=0;i<16;++i){p["material"]=i;doc["meshes"].array[0]["primitives"].array.push_back(p);}}assert(static_models::validate_glb(glb(doc,bin),info,why)&&info.material_count==16);
    // a collision mesh next to several visible materials: collision materials are not visible materials
    doc=with_collision();{auto p=doc["meshes"].array[0]["primitives"].array[0];p["material"]=2;doc["meshes"].array[0]["primitives"].array.push_back(p);doc["materials"].array.push_back(mods::json::parse(R"({"alphaMode":"MASK","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})"));}
    {auto data=glb(doc,cube);assert(static_models::validate_glb(data,info,why,true));assert(info.material_count==2&&info.triangle_count==24&&info.images.size()==1&&data[info.images[0].offset]==137&&info.image_offset==info.images[0].offset);}
    // collision from visible triangles checks every visible material's col_* name
    doc=cube_document();{auto p=doc["meshes"].array[0]["primitives"].array[0];p["material"]=1;doc["meshes"].array[0]["primitives"].array.push_back(p);auto m=doc["materials"].array[0];m["name"]="col_wood";doc["materials"].array.push_back(m);doc["materials"].array[0]["name"]="col_sand";}
    assert(static_models::validate_glb(glb(doc,cube),info,why,true)&&info.material_count==2);
    doc["materials"].array[1]["name"]="col_lava";assert(static_models::validate_glb(glb(doc,cube),info,why,true)&&static_models::collision_surface("col_lava")==14);
    doc["materials"].array[1]["name"]="col_magma";assert(!static_models::validate_glb(glb(doc,cube),info,why,true));
    // a kit model's GLB: own textures as above, texture-free game materials, and never a game material with a texture
    {static_models::KitInfo kit;const std::vector<std::string> names{"rock","Txe_gake_v"};
        doc=document();doc["materials"].array[0]["name"]="cloth";assert(static_models::validate_kit_glb(glb(doc,bin),names,kit,why)&&kit.own_materials==1&&kit.own_texture_bytes==64*8*4&&kit.triangles==1);
        doc["materials"].array[0]["pbrMetallicRoughness"].object.erase("baseColorTexture");doc["materials"].array[0]["name"]="Txe_gake_v";
        assert(static_models::validate_kit_glb(glb(doc,bin),names,kit,why)&&kit.own_materials==0&&kit.materials==1);
        for(const char* name:{"Txe_gake_v","Txe_gake_v.001","rock","rock.002"}){doc=document();doc["materials"].array[0]["name"]=name;assert(!static_models::validate_kit_glb(glb(doc,bin),names,kit,why)&&why.find("never carries")!=std::string::npos);}
        doc=document();doc["materials"].array[0]["name"]="renamed";doc["materials"].array[0]["extras"]=mods::json::parse(R"({"wwhd":{"kit_material":"Txe_gake_v"}})");
        assert(!static_models::validate_kit_glb(glb(doc,bin),names,kit,why));  // a material of the editor's export_glb, renamed
        doc=document();doc["materials"].array[0]["name"]="cloth";doc["meshes"].array[0]["name"]="model_col";assert(!static_models::validate_kit_glb(glb(doc,bin),names,kit,why));  // nothing visible
    }
    std::cout<<"static model validation tests passed\n";
}
