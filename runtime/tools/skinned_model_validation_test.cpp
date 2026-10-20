#include "mods/skinned_model_validation.h"
#include "static_model_fixture.h"
#include <cassert>
#include <iostream>
using mods::json::Value;
struct Fixture {
    Value doc=static_model_fixture::document();std::vector<uint8_t> bin=static_model_fixture::binary();
    size_t add(std::vector<uint8_t> bytes,unsigned component,const char*type,unsigned count){while(bin.size()%4)bin.push_back(0);Value view;view["buffer"]=0;view["byteOffset"]=int(bin.size());view["byteLength"]=int(bytes.size());const size_t vi=doc["bufferViews"].array.size();doc["bufferViews"].array.push_back(view);bin.insert(bin.end(),bytes.begin(),bytes.end());doc["buffers"].array[0]["byteLength"]=int(bin.size());while(bin.size()%4)bin.push_back(0);Value a;a["bufferView"]=int(vi);a["componentType"]=int(component);a["type"]=type;a["count"]=int(count);const size_t ai=doc["accessors"].array.size();doc["accessors"].array.push_back(a);return ai;}
    size_t floats(const std::vector<float>& values,const char*type,unsigned count){std::vector<uint8_t>b;for(float value:values){uint32_t bits;std::memcpy(&bits,&value,4);static_model_fixture::word(b,bits);}return add(b,5126,type,count);}
    Fixture(){doc["nodes"]=mods::json::parse(R"([{"mesh":0,"skin":0},{"name":"Root","children":[2]},{"name":"Head","translation":[0,1,0]}])");doc["scenes"].array[0]["nodes"]=mods::json::parse("[0,1]");const auto ji=add({0,1,0,0,0,1,0,0,0,1,0,0},5121,"VEC4",3);const auto wi=floats({.5,.5,0,0,.5,.5,0,0,.5,.5,0,0},"VEC4",3);std::vector<float>inverse(32);for(unsigned n=0;n<2;++n)for(unsigned k=0;k<4;++k)inverse[n*16+k*5]=1;inverse[16+13]=-1;const auto ii=floats(inverse,"MAT4",2);doc["skins"]=mods::json::parse("[{\"joints\":[1,2],\"skeleton\":1}]");doc["skins"].array[0]["inverseBindMatrices"]=int(ii);auto&at=doc["meshes"].array[0]["primitives"].array[0]["attributes"];at["JOINTS_0"]=int(ji);at["WEIGHTS_0"]=int(wi);}
    std::vector<uint8_t> glb()const{return static_model_fixture::glb(doc,bin);}
    size_t offset(size_t ai)const{const auto&a=doc.get("accessors").array[ai];const auto&v=doc.get("bufferViews").array[size_t(a.get("bufferView").number)];return size_t(v.get("byteOffset").number);}
    void put(size_t offset,float value){uint32_t bits;std::memcpy(&bits,&value,4);for(unsigned k=0;k<4;++k)bin[offset+k]=uint8_t(bits>>(8*k));}
    void clip(const char*name="idle",float duration=1){const auto time=floats({0,duration},"SCALAR",2);const auto value=floats({0,1,0,0,2,0},"VEC3",2);Value a=mods::json::parse(R"({"samplers":[{"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]})");a["name"]=name;a["samplers"].array[0]["input"]=int(time);a["samplers"].array[0]["output"]=int(value);doc["animations"].type=Value::Array;doc["animations"].array.push_back(a);}
};
// a second primitive with the first one's skinned vertices and material m (a new material sharing the texture)
static void add_primitive(Fixture& x,unsigned m){auto p=x.doc["meshes"].array[0]["primitives"].array[0];p["material"]=int(m);x.doc["meshes"].array[0]["primitives"].array.push_back(p);while(x.doc["materials"].array.size()<=m)x.doc["materials"].array.push_back(x.doc["materials"].array[0]);}
// material m gets its own embedded w x h RGBA PNG (header, one IDAT and IEND with valid CRCs; the validator reads no pixels)
static void add_texture(Fixture& x,unsigned m,uint32_t w,uint32_t h){
    auto crc=[](const std::vector<uint8_t>&d,size_t at,size_t n){uint32_t c=0xFFFFFFFFu;for(size_t i=at;i<at+n;++i){c^=d[i];for(unsigned k=0;k<8;++k)c=(c>>1)^(0xEDB88320u&uint32_t(-int32_t(c&1)));}return c^0xFFFFFFFFu;};
    std::vector<uint8_t> png={137,80,78,71,13,10,26,10};
    auto chunk=[&](const char*kind,std::vector<uint8_t> body){auto be=[&](uint32_t v){for(int k=3;k>=0;--k)png.push_back(uint8_t(v>>(8*k)));};be(uint32_t(body.size()));const size_t at=png.size();png.insert(png.end(),kind,kind+4);png.insert(png.end(),body.begin(),body.end());be(crc(png,at,body.size()+4));};
    chunk("IHDR",{uint8_t(w>>24),uint8_t(w>>16),uint8_t(w>>8),uint8_t(w),uint8_t(h>>24),uint8_t(h>>16),uint8_t(h>>8),uint8_t(h),8,6,0,0,0});chunk("IDAT",{120,1,0,0,0,0,0});chunk("IEND",{});
    while(x.bin.size()%4)x.bin.push_back(0);Value view;view["buffer"]=0;view["byteOffset"]=int(x.bin.size());view["byteLength"]=int(png.size());const size_t vi=x.doc["bufferViews"].array.size();x.doc["bufferViews"].array.push_back(view);x.bin.insert(x.bin.end(),png.begin(),png.end());while(x.bin.size()%4)x.bin.push_back(0);x.doc["buffers"].array[0]["byteLength"]=int(x.bin.size());
    Value image;image["bufferView"]=int(vi);image["mimeType"]="image/png";x.doc["images"].array.push_back(image);Value texture;texture["source"]=int(x.doc["images"].array.size()-1);x.doc["textures"].array.push_back(texture);
    x.doc["materials"].array[m]["pbrMetallicRoughness"]["baseColorTexture"]["index"]=int(x.doc["textures"].array.size()-1);}
int main(){skinned_models::ModelInfo info;std::string why;Fixture f;auto valid=[&](const Fixture&x){return skinned_models::validate_glb(x.glb(),info,why);};auto bad=[&](const Fixture&x){assert(!valid(x)&&info.joints==0&&!why.empty());};assert(valid(f)&&info.joints==2&&info.vertex_count==3&&info.clips.empty()&&(info.joint_names==std::vector<std::string>{"Root","Head"})&&(info.joint_parents==std::vector<int>{-1,0}));assert(info.minimum[0]<-199&&info.maximum[0]>199);const auto original=f.glb();assert(original[info.image_offset]==137&&original[info.image_offset+1]=='P');
    assert(info.joint_look_compatible==std::vector<bool>({true,true}));
    auto rotated=f;rotated.doc["nodes"].array[1]["rotation"]=mods::json::parse("[0,1,0,0]");
    for(unsigned n=0;n<2;++n){rotated.put(rotated.offset(6)+(n*16)*4,-1);rotated.put(rotated.offset(6)+(n*16+10)*4,-1);}
    assert(valid(rotated)&&info.joint_look_compatible==std::vector<bool>({false,false})); // rotated ancestor blocks descendants, but skin remains valid
    rotated=f;rotated.doc["nodes"].array[2]["rotation"]=mods::json::parse("[0,1,0,0]");rotated.put(rotated.offset(6)+16*4,-1);rotated.put(rotated.offset(6)+26*4,-1);
    assert(valid(rotated)&&info.joint_look_compatible==std::vector<bool>({true,false}));
    rotated=f;rotated.doc["nodes"].array[2]["rotation"]=mods::json::parse("[0,0,0,-1]");assert(valid(rotated)&&info.joint_look_compatible[1]); // sign-equivalent identity
    auto x=f;x.doc["skins"].array[0]["joints"]=mods::json::parse("[1,1]");bad(x);x=f;x.doc["skins"].array[0]["joints"]=mods::json::parse("[2,1]");bad(x);x=f;x.doc["nodes"].array[2]["name"]="Root";bad(x);x=f;x.doc["nodes"].array[2]["scale"]=mods::json::parse("[2,1,1]");bad(x);x=f;x.doc["nodes"].array[0]["translation"]=mods::json::parse("[1,0,0]");bad(x);x=f;x.doc["nodes"].array[1].object.erase("children");bad(x);
    x=f;x.bin[x.offset(4)]=2;bad(x);x=f;x.put(x.offset(5),-.5);bad(x);x=f;x.put(x.offset(5),.25);bad(x);x=f;x.put(x.offset(5)+8,.1);bad(x);x=f;x.put(x.offset(6)+4,1);bad(x);x=f;x.doc["accessors"].array[6]["count"]=1;bad(x);
    // several materials, cut-outs and double-sided faces: the rigid model rules, every primitive skinned
    x=f;x.doc["materials"].array[0]["alphaMode"]="MASK";x.doc["materials"].array[0]["alphaCutoff"]=0.25;assert(valid(x)&&info.cutout_count==1&&info.material_count==1);
    x=f;x.doc["materials"].array[0]["doubleSided"]=true;assert(valid(x)&&info.double_sided_count==1);
    x=f;x.doc["materials"].array[0]["alphaMode"]="BLEND";bad(x);assert(why.find("alphaMode BLEND")!=std::string::npos);
    x=f;add_primitive(x,1);assert(valid(x)&&info.material_count==2&&info.vertex_count==6&&info.triangle_count==2&&info.images.size()==1&&info.joints==2);
    x=f;add_primitive(x,1);x.doc["meshes"].array[0]["primitives"].array[1]["attributes"].object.erase("WEIGHTS_0");bad(x); // every primitive needs its skin weights
    x=f;add_primitive(x,1);{const auto wi=x.floats({1,0,0,0,.5,.25,0,0,1,0,0,0},"VEC4",3);x.doc["meshes"].array[0]["primitives"].array[1]["attributes"]["WEIGHTS_0"]=int(wi);}bad(x);assert(why.find("normalized")!=std::string::npos);
    x=f;add_primitive(x,1);{const auto ji=x.add({0,1,0,0,0,2,0,0,0,1,0,0},5121,"VEC4",3);x.doc["meshes"].array[0]["primitives"].array[1]["attributes"]["JOINTS_0"]=int(ji);}bad(x); // joint 2 outside the skeleton
    x=f;add_primitive(x,1);x.doc["materials"].array.push_back(x.doc["materials"].array[0]);bad(x);assert(why.find("every material must be used")!=std::string::npos);
    x=f;for(unsigned m=1;m<16;++m)add_primitive(x,m);assert(valid(x)&&info.material_count==16);
    add_primitive(x,16);bad(x);assert(why.find("at most 16")!=std::string::npos);
    x=f;for(unsigned m=0;m<32;++m)add_primitive(x,0);bad(x);assert(why.find("32 primitives")!=std::string::npos); // 33 primitives
    // the texture budget: 1 MiB of decoded textures per model (512 x 512 and 512 x 256 together are 1.5 MiB)
    x=f;add_primitive(x,1);add_primitive(x,2);add_texture(x,1,256,256);add_texture(x,2,512,256);assert(valid(x)&&info.images.size()==3&&info.texture_bytes==size_t(256*256*4+512*256*4+64*8*4));
    x=f;add_primitive(x,1);add_primitive(x,2);add_texture(x,1,512,512);add_texture(x,2,512,256);bad(x);assert(why.find("1 MiB")!=std::string::npos);
    x=f;assert(valid(x)&&info.images.size()==1&&x.glb()[info.images[0].offset]==137&&info.images[0].offset==info.image_offset);
    x=f;x.clip();assert(valid(x)&&info.clips.size()==1&&info.clips[0].name=="idle"&&info.clips[0].frames==31&&info.maximum[0]>299);x.doc["animations"].array[0]["samplers"].array[0]["interpolation"]="CUBICSPLINE";bad(x);
    x=f;x.clip();x.doc["animations"].array[0]["channels"].array[0]["target"]["path"]="scale";bad(x);x=f;x.clip("Bad Name");bad(x);x=f;x.clip("idle",31);bad(x);x=f;x.clip("idle",30);assert(valid(x)&&info.clips[0].frames==901);x=f;x.clip();x.doc["animations"].array.push_back(x.doc["animations"].array[0]);bad(x);x=f;x.clip();x.doc["animations"].array[0]["extensions"]=mods::json::parse("{\"ignored\":{}}");bad(x);
    x=f;x.clip();const auto time=size_t(x.doc.get("animations").array[0].get("samplers").array[0].get("input").number);x.put(x.offset(time)+4,0);bad(x);x=f;x.clip();x.doc["animations"].array[0]["channels"].array[0]["target"]["node"]=0;bad(x);
    x=f;x.doc["skins"].array[0]["joints"].array.resize(33,Value(1));bad(x);
    x=f;x.doc["nodes"].array[1]["rotation"]=mods::json::parse("[0,0,0,2]");bad(x);
    x=f;x.doc["nodes"].array.push_back(mods::json::parse("{\"name\":\"Leg\"}"));x.doc["nodes"].array[1]["children"].array.push_back(Value(3));x.doc["skins"].array[0]["joints"].array.push_back(Value(3));std::vector<float> three_inverse(48);for(unsigned n=0;n<3;++n)for(unsigned k=0;k<4;++k)three_inverse[n*16+k*5]=1;three_inverse[29]=-1;x.doc["skins"].array[0]["inverseBindMatrices"]=int(x.floats(three_inverse,"MAT4",3));x.bin[x.offset(4)+2]=2;x.put(x.offset(5),.25);x.put(x.offset(5)+4,.25);x.put(x.offset(5)+8,.5);bad(x); // three real positive influences
    Fixture large;large.doc["nodes"].array.resize(33);large.doc["nodes"].array[0]=mods::json::parse("{\"mesh\":0,\"skin\":0}");large.doc["skins"].array[0]["joints"].array.clear();std::vector<float> matrices(32*16);
    for(unsigned n=0;n<32;++n){auto&node=large.doc["nodes"].array[n+1];node=Value{};node["name"]="Bone"+std::to_string(n);large.doc["skins"].array[0]["joints"].array.push_back(Value(int(n+1)));if(n)large.doc["nodes"].array[1]["children"].array.push_back(Value(int(n+1)));for(unsigned k=0;k<4;++k)matrices[n*16+k*5]=1;}
    large.doc["nodes"].array[1]["children"].type=Value::Array;large.doc["skins"].array[0]["inverseBindMatrices"]=int(large.floats(matrices,"MAT4",32));assert(valid(large)&&info.joints==32&&info.joint_parents[0]==-1);for(unsigned n=1;n<32;++n)assert(info.joint_parents[n]==0);
    const auto time_index=large.floats({0,30},"SCALAR",2),translation_index=large.floats({0,0,0,0,1,0},"VEC3",2),rotation_index=large.floats({0,0,0,1,0,0,0,1},"VEC4",2);Value clip;clip["samplers"]=mods::json::parse("[{\"interpolation\":\"LINEAR\"},{\"interpolation\":\"STEP\"}]");for(unsigned n=0;n<2;++n){clip["samplers"].array[n]["input"]=int(time_index);clip["samplers"].array[n]["output"]=int(n?rotation_index:translation_index);}clip["channels"].type=Value::Array;
    for(unsigned n=1;n<=32;++n)for(unsigned k=0;k<2;++k){Value channel;channel["sampler"]=int(k);channel["target"]["node"]=int(n);channel["target"]["path"]=k?"rotation":"translation";clip["channels"].array.push_back(channel);}large.doc["animations"].type=Value::Array;for(unsigned n=0;n<2;++n){clip["name"]="clip"+std::to_string(n);large.doc["animations"].array.push_back(clip);}assert(valid(large)&&info.clips.size()==2);clip["name"]="third";large.doc["animations"].array.push_back(clip);assert(valid(large));clip["name"]="fourth";large.doc["animations"].array.push_back(clip);bad(large); // native three-component rotation curves, 2 MiB combined sampled payload
    x=f;x.doc["nodes"].array[0]["matrix"]=mods::json::parse("[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]");assert(valid(x));x.doc["nodes"].array[0]["translation"]=mods::json::parse("[0,0,0]");bad(x);
    x=f;x.doc["animations"]=Value{};bad(x);
    x=f;x.doc["nodes"].array[2]["children"]=Value{};bad(x);x=f;x.doc["buffers"].array[0]["uri"]=Value{};bad(x);x=f;x.doc["images"].array[0]["uri"]=Value{};bad(x);
    std::cout<<"skinned model validation tests passed\n";
}
