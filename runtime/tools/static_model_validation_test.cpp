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
    doc=document();doc["materials"].array[0]["doubleSided"]=true;bad(doc,bin);
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
    doc=with_collision();doc["nodes"].array[2]["scale"]=mods::json::parse("[1000,1,1]");bad(doc,cube);
    doc=with_collision();doc["nodes"].array[1]["translation"]=mods::json::parse("[201,0,0]");bad(doc,cube);
    doc=with_collision();doc["scenes"].array[0]["nodes"].array.pop_back();bad(doc,cube);
    doc=with_collision();doc["meshes"].array[1]["primitives"].array[0]["mode"]=1;bad(doc,cube);
    doc=cube_document();assert(static_models::validate_glb(glb(doc,cube),info,why,true));doc["nodes"].array[0]["translation"]=mods::json::parse("[201,0,0]");assert(!static_models::validate_glb(glb(doc,cube),info,why,true)&&info.vertex_count==0);
    doc=with_collision();auto flat=cube;for(size_t n=0;n<96;++n)flat[n]=0;bad(doc,flat);
    doc=with_collision();auto many=doc["meshes"].array[1]["primitives"].array[0];doc["meshes"].array[1]["primitives"].array.assign(64,many);doc["nodes"].array.push_back(mods::json::parse("{\"mesh\":1}"));doc["scenes"].array[0]["nodes"].array.push_back(3);for(unsigned n=0;n<4;++n){doc["nodes"].array.push_back(mods::json::parse("{\"mesh\":1}"));doc["scenes"].array[0]["nodes"].array.push_back(int(4+n));}bad(doc,cube);
    std::cout<<"static model validation tests passed\n";
}
