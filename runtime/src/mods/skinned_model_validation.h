// Original GLB skin/clip validation. No player template resources are needed here.
#pragma once
#include "static_model_validation.h"
#include <algorithm>
#include <set>
#include <map>
namespace skinned_models {
struct Clip {std::string name;unsigned frames=0;};
struct ModelInfo:static_models::ModelInfo {unsigned joints=0;std::vector<std::string> joint_names;std::vector<int> joint_parents;std::vector<bool> joint_look_compatible;std::vector<Clip> clips;};
namespace detail {
using mods::json::Value;
using Matrix=std::array<double,16>;
inline Matrix identity(){Matrix m{};for(size_t k=0;k<4;++k)m[k*4+k]=1;return m;}
inline Matrix multiply(const Matrix&a,const Matrix&b){Matrix m{};for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c)for(size_t k=0;k<4;++k)m[r*4+c]+=a[r*4+k]*b[k*4+c];return m;}
inline bool no_extensions(const Value& v){if(v.type==Value::Object){if(v.get("extensions").type!=Value::Null&&!(v.get("extensions").type==Value::Object&&v.get("extensions").object.empty()))return false;for(const auto&[k,x]:v.object)if(!no_extensions(x))return false;}else if(v.type==Value::Array)for(const auto&x:v.array)if(!no_extensions(x))return false;return true;}
inline bool vector(const Value&v,size_t n,std::vector<double>&out){out.clear();if(v.type!=Value::Array||v.array.size()!=n)return false;for(const auto&x:v.array){if(x.type!=Value::Number||!std::isfinite(x.number))return false;out.push_back(x.number);}return true;}
inline bool transform(const Value&node,Matrix& m){
    if(node.object.contains("matrix"))return false;std::vector<double> q{0,0,0,1},t{0,0,0},s{1,1,1};
    if(node.object.contains("rotation")&&!vector(node.get("rotation"),4,q))return false;if(node.object.contains("translation")&&!vector(node.get("translation"),3,t))return false;if(node.object.contains("scale")&&!vector(node.get("scale"),3,s))return false;
    if(s!=std::vector<double>{1,1,1})return false;double norm=0;for(double v:q)norm+=v*v;if(!std::isfinite(norm)||std::fabs(norm-1)>=1e-4)return false;
    const double x=q[0],y=q[1],z=q[2],w=q[3];m={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),t[0],2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),t[1],2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),t[2],0,0,0,1};return true;
}
inline bool bone_name(const std::string&s){if(s.empty()||s.size()>31||!((s[0]>='a'&&s[0]<='z')||(s[0]>='A'&&s[0]<='Z')))return false;for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;return true;}
inline bool clip_name(const std::string&s){if(s.empty()||s.size()>32)return false;for(char c:s)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;return true;}
inline void word(std::vector<uint8_t>&b,uint32_t n){for(unsigned k=0;k<4;++k)b.push_back(uint8_t(n>>(8*k)));}
}
inline bool validate_glb(std::span<const uint8_t>b,ModelInfo&info,std::string&why){
    using namespace detail;using static_models::integer;info={};auto fail=[&](const char*s){why=s;return false;};
    if(b.size()<28||b.size()>static_models::kMaxSourceBytes||std::memcmp(b.data(),"glTF",4)||static_models::le32(b.data()+4)!=2||static_models::le32(b.data()+8)!=b.size())return fail("invalid skinned GLB header");
    const size_t jl=static_models::le32(b.data()+12);if(jl%4||jl>b.size()-28||static_models::le32(b.data()+16)!=0x4E4F534A)return fail("invalid skinned GLB JSON");const size_t base=28+jl,bl=static_models::le32(b.data()+20+jl);
    if(bl%4||bl!=b.size()-base||static_models::le32(b.data()+24+jl)!=0x004E4942)return fail("invalid skinned GLB BIN");
    Value doc;try{doc=mods::json::parse(std::string(reinterpret_cast<const char*>(b.data()+20),jl));}catch(...){return fail("invalid skinned GLB JSON");}
    if(!no_extensions(doc))return fail("skinned GLB extensions unsupported");
    const auto& skins=doc.get("skins");const auto& nodes=doc.get("nodes");if(skins.type!=Value::Array||skins.array.size()!=1||nodes.type!=Value::Array||nodes.array.empty()||nodes.array.size()>64)return fail("one skin and bounded nodes required");
    const auto& skin=skins.array[0];const auto& joints=skin.get("joints");if(joints.type!=Value::Array||joints.array.empty()||joints.array.size()>32)return fail("skin joints must be 1..32");
    const auto& buffers=doc.get("buffers");if(buffers.type!=Value::Array||buffers.array.size()!=1||buffers.array[0].object.contains("uri")||!integer(buffers.array[0].get("byteLength"),bl>=3?bl-3:0,bl))return fail("invalid skin buffer");const size_t used=size_t(buffers.array[0].get("byteLength").number);
    struct Access {size_t count=0,width=0;std::vector<double> data;};
    auto accessor=[&](const Value&index,const char*type,size_t width,std::initializer_list<unsigned>components,Access&out){
        const auto& list=doc.get("accessors");if(list.type!=Value::Array||list.array.empty()||!integer(index,0,list.array.size()-1))return false;const auto&a=list.array[size_t(index.number)];
        if(a.get("type").string()!=type||!integer(a.get("count"),1,4096)||a.object.contains("sparse")||(a.get("normalized").type!=Value::Null&&(a.get("normalized").type!=Value::Bool||a.get("normalized").boolean))||!integer(a.get("componentType"),0,65535))return false;
        const unsigned component=unsigned(a.get("componentType").number);if(std::find(components.begin(),components.end(),component)==components.end())return false;const size_t size=component==5121?1:component==5123?2:4;
        const auto& views=doc.get("bufferViews");if(views.type!=Value::Array||views.array.empty()||!integer(a.get("bufferView"),0,views.array.size()-1))return false;const auto&v=views.array[size_t(a.get("bufferView").number)];
        if((v.get("buffer").type!=Value::Null&&!integer(v.get("buffer"),0,0))||!integer(v.get("byteLength"),1,used)||(v.get("byteOffset").type!=Value::Null&&!integer(v.get("byteOffset"),0,used)))return false;
        const size_t off=v.get("byteOffset").type==Value::Null?0:size_t(v.get("byteOffset").number),len=size_t(v.get("byteLength").number),count=size_t(a.get("count").number),packed=width*size;
        if(off>used||len>used-off||(a.get("byteOffset").type!=Value::Null&&!integer(a.get("byteOffset"),0,len)))return false;const size_t local=a.get("byteOffset").type==Value::Null?0:size_t(a.get("byteOffset").number);
        if(v.get("byteStride").type!=Value::Null&&!integer(v.get("byteStride"),packed,252))return false;const size_t stride=v.get("byteStride").type==Value::Null?packed:size_t(v.get("byteStride").number);
        if(stride%size||local%size||local>len||packed>len-local||(count-1)>(len-local-packed)/stride)return false;
        out={count,width,{}};out.data.reserve(count*width);for(size_t n=0;n<count;++n)for(size_t k=0;k<width;++k){const auto*p=b.data()+base+off+local+n*stride+k*size;double value;if(component==5121)value=*p;else if(component==5123)value=unsigned(p[0])|(unsigned(p[1])<<8);else{const uint32_t bits=static_models::le32(p);float f;std::memcpy(&f,&bits,4);value=f;}if(!std::isfinite(value))return false;out.data.push_back(value);}return true;
    };
    std::vector<int> parents(nodes.array.size(),-1),lookup(nodes.array.size(),-1);for(size_t n=0;n<nodes.array.size();++n){const auto&children=nodes.array[n].get("children");if((nodes.array[n].object.contains("children")&&children.type!=Value::Array))return fail("invalid skeleton children");for(const auto&child:children.array){if(!integer(child,0,nodes.array.size()-1)||parents[size_t(child.number)]!=-1)return fail("invalid or shared joint parent");parents[size_t(child.number)]=int(n);}}
    for(size_t n=0;n<joints.array.size();++n){const auto&j=joints.array[n];if(!integer(j,0,nodes.array.size()-1)||lookup[size_t(j.number)]!=-1)return fail("invalid or duplicate joint");lookup[size_t(j.number)]=int(n);}
    std::vector<Matrix> world;std::vector<bool> look_compatible;std::vector<int> bone_parent;std::vector<double> translation_max;std::set<std::string> names;std::vector<std::string> joint_names;
    for(size_t n=0;n<joints.array.size();++n){const size_t index=size_t(joints.array[n].number);const int p=parents[index],parent=p<0?-1:lookup[size_t(p)];if((p>=0&&parent<0)||parent>=int(n)||(n&&parent<0))return fail("connected parent-first joints required");
        const auto&node=nodes.array[index];const auto name=node.get("name").string();if(!bone_name(name)||!names.insert(name).second)return fail("invalid or duplicate bone name");joint_names.push_back(name);Matrix local;if(!transform(node,local))return fail("unit scale TRS joints required");bool upright=parent<0||look_compatible[size_t(parent)];for(size_t r=0;r<3;++r)for(size_t c=0;c<3;++c)upright=upright&&std::fabs(local[r*4+c]-(r==c?1.0:0.0))<=1e-4;look_compatible.push_back(upright);const auto combined=parent<0?local:multiply(world[size_t(parent)],local);for(double x:combined)if(!std::isfinite(x)||std::fabs(x)>10000)return fail("skeleton coordinate range");world.push_back(combined);bone_parent.push_back(parent);translation_max.push_back(std::sqrt(local[3]*local[3]+local[7]*local[7]+local[11]*local[11]));
    }
    if(skin.object.contains("skeleton")&&(!integer(skin.get("skeleton"),0,nodes.array.size()-1)||skin.get("skeleton").number!=joints.array[0].number))return fail("skin root mismatch");
    Access inverse;if(!accessor(skin.get("inverseBindMatrices"),"MAT4",16,{5126},inverse)||inverse.count!=world.size())return fail("invalid inverse bind matrices");
    for(size_t n=0;n<world.size();++n)for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c){double expected=r==3?(c==3?1:0):c==3?-(world[n][r]*world[n][3]+world[n][4+r]*world[n][7]+world[n][8+r]*world[n][11]):world[n][c*4+r];if(std::fabs(inverse.data[n*16+c*4+r]-expected)>1e-4)return fail("inverse bind differs from hierarchy");}
    int mesh=-1;for(size_t n=0;n<nodes.array.size();++n)if(nodes.array[n].object.contains("mesh")){if(mesh>=0)return fail("one skin mesh instance required");mesh=int(n);}
    Matrix mesh_transform;bool mesh_identity=false;if(mesh>=0){const auto&node=nodes.array[size_t(mesh)];if(node.object.contains("matrix")){std::vector<double> values;mesh_identity=vector(node.get("matrix"),16,values)&&!node.object.contains("translation")&&!node.object.contains("rotation")&&!node.object.contains("scale");if(mesh_identity)for(size_t k=0;k<16;++k)mesh_identity=mesh_identity&&values[k]==(k%5==0?1:0);}else mesh_identity=transform(node,mesh_transform)&&mesh_transform==identity();}
    if(mesh<0||parents[size_t(mesh)]!=-1||!integer(nodes.array[size_t(mesh)].get("mesh"),0,0)||!integer(nodes.array[size_t(mesh)].get("skin"),0,0)||!mesh_identity)return fail("identity root mesh with skin 0 required");
    for(size_t n=0;n<nodes.array.size();++n)if(int(n)!=mesh&&nodes.array[n].object.contains("skin"))return fail("unexpected extra skin binding");
    // Every triangle primitive is skinned to the one skeleton (one primitive per material; up to 32, as for rigid models).
    const auto& meshes=doc.get("meshes");if(meshes.type!=Value::Array||meshes.array.size()!=1||meshes.array[0].get("primitives").type!=Value::Array||meshes.array[0].get("primitives").array.empty())return fail("one skinned mesh with triangle primitives required");
    if(meshes.array[0].get("primitives").array.size()>static_models::kMaxPrimitives)return fail("the visible mesh has more than 32 primitives");
    struct Skin {Access indices,weights,positions;};std::vector<Skin> skinned;
    for(const auto&primitive:meshes.array[0].get("primitives").array){const auto&attrs=primitive.get("attributes");if(attrs.type!=Value::Object||attrs.object.size()!=5)return fail("invalid skinned attributes");
        Skin s;if(!accessor(attrs.get("JOINTS_0"),"VEC4",4,{5121,5123},s.indices)||!accessor(attrs.get("WEIGHTS_0"),"VEC4",4,{5126},s.weights)||!accessor(attrs.get("POSITION"),"VEC3",3,{5126},s.positions)||s.indices.count!=s.weights.count||s.positions.count!=s.weights.count)return fail("invalid skin vertex attributes");
        for(size_t n=0;n<s.weights.count;++n){double sum=0;std::set<unsigned> positive;for(size_t k=0;k<4;++k){const double j=s.indices.data[n*4+k],w=s.weights.data[n*4+k];if(j>=world.size()||w<0)return fail("invalid skin weights or joints");sum+=w;if(w>0&&!positive.insert(unsigned(j)).second)return fail("duplicate positive skin influence");}if(positive.empty()||positive.size()>2||std::fabs(sum-1)>=1e-4)return fail("skin needs one or two normalized influences");}
        skinned.push_back(std::move(s));
    }
    const auto& animations=doc.get("animations");if((doc.object.contains("animations")&&animations.type!=Value::Array)||animations.array.size()>8)return fail("at most eight clips allowed");std::set<std::string> clips_seen;std::vector<Clip> clips;uint64_t sampled_bytes=0;
    for(const auto&animation:animations.array){const auto name=animation.get("name").string();if(!clip_name(name)||!clips_seen.insert(name).second)return fail("invalid or duplicate clip name");const auto&channels=animation.get("channels");const auto&samplers=animation.get("samplers");if(channels.type!=Value::Array||channels.array.empty()||channels.array.size()>2*world.size()||samplers.type!=Value::Array||samplers.array.empty())return fail("invalid clip channels");std::set<std::pair<size_t,std::string>> targets;double last=0;size_t component_count=0;
        for(const auto&channel:channels.array){if(!integer(channel.get("sampler"),0,samplers.array.size()-1))return fail("invalid animation sampler");const auto&sampler=samplers.array[size_t(channel.get("sampler").number)];const auto interpolation=sampler.get("interpolation").string("LINEAR");if(interpolation!="LINEAR"&&interpolation!="STEP")return fail("animation interpolation unsupported");const auto&target=channel.get("target");const auto path=target.get("path").string();if(!integer(target.get("node"),0,nodes.array.size()-1)||lookup[size_t(target.get("node").number)]<0||(path!="rotation"&&path!="translation")||!targets.insert({size_t(target.get("node").number),path}).second)return fail("invalid or duplicate clip joint target");
            Access times,values;const size_t width=path=="rotation"?4:3;if(!accessor(sampler.get("input"),"SCALAR",1,{5126},times)||!accessor(sampler.get("output"),width==4?"VEC4":"VEC3",width,{5126},values)||times.count!=values.count)return fail("invalid clip keyframes");
            for(size_t k=0;k<times.count;++k){if(times.data[k]<0||(k&&times.data[k]<=times.data[k-1]))return fail("clip times must increase");double norm=0;for(size_t c=0;c<width;++c){const double v=values.data[k*width+c];if(width==3&&std::fabs(v)>100)return fail("clip translation range");norm+=v*v;}if(width==4&&norm<=1e-16)return fail("zero clip quaternion");if(width==3){const size_t joint=size_t(lookup[size_t(target.get("node").number)]);translation_max[joint]=std::max(translation_max[joint],std::sqrt(norm));}}
            last=std::max(last,times.data.back());component_count+=3; // native quaternion samples become three continuous Euler curves
        }
        if(last<=0||last>31)return fail("clip duration invalid");const double rounded=std::ceil(last*30-1e-5);if(rounded<1||rounded>900)return fail("clip exceeds 900 frame duration");const unsigned end=unsigned(rounded);sampled_bytes+=uint64_t(end+2)*component_count*4;if(sampled_bytes>2*1024*1024)return fail("sampled clips exceed 2 MiB");clips.push_back({name,end+1});
    }
    for(const auto&image:doc.get("images").array)if(image.object.contains("uri"))return fail("embedded skin texture required");
    // Validate every static field using the existing strict parser. Skin/clip fields
    // are removed only after independent checks; the original BIN remains unchanged.
    auto clean=doc;clean.object.erase("skins");clean.object.erase("animations");for(auto&node:clean["nodes"].array)node.object.erase("skin");for(auto&primitive:clean["meshes"].array[0]["primitives"].array){auto&ca=primitive["attributes"];ca.object.erase("JOINTS_0");ca.object.erase("WEIGHTS_0");}auto text=mods::json::dump(clean);while(text.size()%4)text+=' ';std::vector<uint8_t> projection;word(projection,0x46546C67);word(projection,2);word(projection,uint32_t(28+text.size()+bl));word(projection,uint32_t(text.size()));word(projection,0x4E4F534A);projection.insert(projection.end(),text.begin(),text.end());word(projection,uint32_t(bl));word(projection,0x004E4942);projection.insert(projection.end(),b.begin()+base,b.end());
    static_models::ModelInfo geometry;if(!static_models::validate_glb(projection,geometry,why))return false;
    // Several materials, cut-outs, double-sided faces and the texture budget: the rigid model rules above.
    // The BIN chunk is unchanged: move every image offset from the projection back into the original file.
    for(auto&image:geometry.images)image.offset=base+(image.offset-(28+text.size()));geometry.image_offset=geometry.images.front().offset;
    // Unit-scale skeleton rotations (including future look-at) preserve norm. Sum
    // maximal authored translation lengths along each chain and bound each inverse
    // bound vertex; normalized nonnegative skinning is a convex combination.
    double radius=0;for(const auto&[indices,weights,positions]:skinned)for(size_t v=0;v<positions.count;++v)for(size_t k=0;k<4;++k)if(weights.data[v*4+k]>0){const size_t j=size_t(indices.data[v*4+k]);double norm=0;for(size_t r=0;r<3;++r){double value=inverse.data[j*16+12+r];for(size_t c=0;c<3;++c)value+=inverse.data[j*16+c*4+r]*positions.data[v*3+c];norm+=value*value;}double bound=std::sqrt(norm);for(int n=int(j);n>=0;n=bone_parent[size_t(n)])bound+=translation_max[size_t(n)];radius=std::max(radius,bound*100);}
    radius=radius*(1.0002+1e-5*world.size())+0.01;if(!std::isfinite(radius)||radius>1000000)return fail("animated model bounds exceed coordinate range");geometry.minimum.fill(-float(radius));geometry.maximum.fill(float(radius));static_cast<static_models::ModelInfo&>(info)=geometry;info.joints=unsigned(world.size());info.joint_names=std::move(joint_names);info.joint_parents=std::move(bone_parent);info.joint_look_compatible=std::move(look_compatible);info.clips=std::move(clips);return true;
}
}
