// Bounded static GLB input shared by package validation and model tooling tests.
#pragma once
#include "mod_json.h"
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <functional>
#include <array>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include <map>
#include <set>
namespace static_models {
inline constexpr size_t kMaxSourceBytes=8*1024*1024;
inline constexpr unsigned kMaxVertices=4096,kMaxTriangles=8192,kMaxTextureDimension=512;
// Several materials (tools/story_mods/multi_material.py): up to 16 materials with one embedded PNG each and up to 32
// triangle primitives; vertex and triangle limits count all primitives together; all colour textures of one model
// take at most 1 MiB decoded (one 512 x 512 RGBA8 texture, the single-material maximum).
inline constexpr unsigned kMaxMaterials=16,kMaxPrimitives=32;
inline constexpr size_t kMaxTextureBytes=1024*1024;
struct Image {size_t offset=0,size=0;unsigned width=0,height=0;};
// texture_width/height and image_offset/size describe the first image; images lists them all.
struct ModelInfo {unsigned vertex_count=0,triangle_count=0,texture_width=0,texture_height=0;size_t image_offset=0,image_size=0;std::array<float,3> minimum{},maximum{};
    unsigned material_count=0,cutout_count=0,double_sided_count=0;size_t texture_bytes=0;std::vector<Image> images;};
inline uint32_t le32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint32_t be32(const uint8_t* p){return uint32_t(p[3])|(uint32_t(p[2])<<8)|(uint32_t(p[1])<<16)|(uint32_t(p[0])<<24);}
// The collision material names a GLB may use (tools/story_mods/collision_mesh.py SEMANTICS; docs/world-objects.md).
// Its surface (story_world_collision::mesh semantic: the list's order), -1 for another name.
inline int collision_surface(const std::string& name){
    int i=0;
    for(const char* m:{"col_stone","col_grass","col_wood","col_sand","col_metal","col_wall","col_objects","col_climb","col_hookshot",
                       "col_water","col_shallow","col_waterfall","col_ice","col_void"}){if(name==m)return i;++i;}
    return -1;
}
inline bool collision_material(const std::string& name){return collision_surface(name)>=0;}
inline bool integer(const mods::json::Value& v,size_t lo,size_t hi){return v.type==mods::json::Value::Number&&std::isfinite(v.number)&&v.number>=double(lo)&&v.number<=double(hi)&&std::floor(v.number)==v.number;}
inline bool validate_glb_strict(std::span<const uint8_t> b,ModelInfo& info,std::string& why,bool collision=false){
    using mods::json::Value;info={};auto fail=[&](const std::string& s){why=s;return false;};
    if(b.size()<28||b.size()>kMaxSourceBytes||std::memcmp(b.data(),"glTF",4)||le32(b.data()+4)!=2||le32(b.data()+8)!=b.size())return fail("invalid or oversized GLB 2.0");
    const size_t jl=le32(b.data()+12);if(jl%4||jl>b.size()-28||le32(b.data()+16)!=0x4E4F534A)return fail("invalid GLB JSON chunk");
    const size_t bh=20+jl,bl=le32(b.data()+bh),base=bh+8;
    if(bl%4||bl!=b.size()-base||le32(b.data()+bh+4)!=0x004E4942)return fail("GLB needs exactly one embedded BIN chunk");
    Value doc;try{doc=mods::json::parse(std::string(reinterpret_cast<const char*>(b.data()+20),jl));}catch(...){return fail("invalid GLB JSON");}
    if(doc.type!=Value::Object||doc.get("asset").get("version").string()!="2.0")return fail("glTF asset version must be 2.0");
    for(const char* key:{"skins","animations","extensionsUsed","extensionsRequired"})if(doc.get(key).type!=Value::Null&&!(doc.get(key).type==Value::Array&&doc.get(key).array.empty()))return fail("static GLB cannot contain skins, animations or extensions");
    std::function<bool(const Value&)> no_extensions=[&](const Value& v){if(v.type==Value::Object){if(v.get("extensions").type!=Value::Null&&!(v.get("extensions").type==Value::Object&&v.get("extensions").object.empty()))return false;for(const auto& [k,x]:v.object)if(!no_extensions(x))return false;}else if(v.type==Value::Array)for(const auto& x:v.array)if(!no_extensions(x))return false;return true;};
    if(!no_extensions(doc))return fail("GLB extensions are unsupported");
    const auto& buffers=doc.get("buffers");if(buffers.type!=Value::Array||buffers.array.size()!=1||buffers.array[0].get("uri").type!=Value::Null||!integer(buffers.array[0].get("byteLength"),bl>=3?bl-3:0,bl))return fail("invalid embedded glTF buffer");
    const size_t used=size_t(buffers.array[0].get("byteLength").number);
    const auto& views=doc.get("bufferViews");const auto& accessors=doc.get("accessors");
    auto view=[&](const Value& index,size_t& off,size_t& len,size_t& stride){
        if(views.type!=Value::Array||views.array.empty()||!integer(index,0,views.array.size()-1))return false;const auto& v=views.array[size_t(index.number)];
        if(!integer(v.get("buffer"),0,0)||!integer(v.get("byteLength"),1,used))return false;
        if(v.get("byteOffset").type!=Value::Null&&!integer(v.get("byteOffset"),0,used))return false;
        off=v.get("byteOffset").type==Value::Null?0:size_t(v.get("byteOffset").number);len=size_t(v.get("byteLength").number);stride=0;
        if(off>used||len>used-off)return false;
        if(v.get("byteStride").type!=Value::Null){if(!integer(v.get("byteStride"),4,252)||size_t(v.get("byteStride").number)%4)return false;stride=size_t(v.get("byteStride").number);}return true;
    };
    const auto& meshes=doc.get("meshes");if(meshes.type!=Value::Array||meshes.array.size()!=1||meshes.array[0].get("primitives").type!=Value::Array||meshes.array[0].get("primitives").array.empty())return fail("static GLB needs one mesh with triangle primitives");
    const auto& mesh=meshes.array[0];const auto& primitives=mesh.get("primitives").array;
    if(primitives.size()>kMaxPrimitives)return fail("the visible mesh has more than 32 primitives");
    if(mesh.get("weights").type!=Value::Null)return fail("static model must use unextended triangles without morphs");
    struct Access {size_t offset=0,count=0,stride=0;unsigned component=0;};
    auto accessor=[&](const Value& index,const char* type,size_t components,bool indices,Access& out){
        if(accessors.type!=Value::Array||accessors.array.empty()||!integer(index,0,accessors.array.size()-1))return false;const auto& a=accessors.array[size_t(index.number)];
        if(a.get("type").string()!=type||!integer(a.get("count"),1,indices?kMaxTriangles*3:kMaxVertices)||a.get("sparse").type!=Value::Null||(a.get("normalized").type!=Value::Null&&(a.get("normalized").type!=Value::Bool||a.get("normalized").boolean)))return false;
        if(!integer(a.get("componentType"),0,65535))return false;unsigned component=unsigned(a.get("componentType").number);if(indices?(component!=5121&&component!=5123&&component!=5125):component!=5126)return false;
        size_t off,len,stride;if(!view(a.get("bufferView"),off,len,stride))return false;const size_t width=(component==5121?1:component==5123?2:4)*components;
        if(a.get("byteOffset").type!=Value::Null&&!integer(a.get("byteOffset"),0,len))return false;const size_t local=a.get("byteOffset").type==Value::Null?0:size_t(a.get("byteOffset").number);const size_t count=size_t(a.get("count").number);
        if(!stride)stride=width;if(stride<width||local% (component==5121?1:component==5123?2:4)||local>len||width>len-local||(count-1)>(len-local-width)/stride)return false;
        out={base+off+local,count,stride,component};return true;
    };
    // Every primitive: indexed triangles with POSITION (and NORMAL, TEXCOORD_0 when it is drawn).
    struct Primitive {Access pos,nrm,uv,idx;size_t material=0;};std::vector<Primitive> prims;size_t vertices=0,triangles=0,drawn_vertices=0,drawn_triangles=0;
    for(const auto& p:primitives){
        if(p.type!=Value::Object||p.get("targets").type!=Value::Null||p.get("extensions").type!=Value::Null||(p.get("mode").type!=Value::Null&&!integer(p.get("mode"),4,4)))return fail("static model must use unextended triangles without morphs");
        Primitive q;const auto& at=p.get("attributes");
        if(at.type!=Value::Object||!accessor(at.get("POSITION"),"VEC3",3,false,q.pos)||!accessor(p.get("indices"),"SCALAR",1,true,q.idx)||q.idx.count%3)return fail("invalid model attributes or indices");
        if(!collision&&(at.object.size()!=3||!accessor(at.get("NORMAL"),"VEC3",3,false,q.nrm)||!accessor(at.get("TEXCOORD_0"),"VEC2",2,false,q.uv)||q.pos.count!=q.nrm.count||q.pos.count!=q.uv.count))return fail("invalid static model attributes");
        if(collision){if(at.get("NORMAL").type!=Value::Null&&(!accessor(at.get("NORMAL"),"VEC3",3,false,q.nrm)||q.nrm.count!=q.pos.count))return fail("invalid collision normals");if(at.get("TEXCOORD_0").type!=Value::Null&&(!accessor(at.get("TEXCOORD_0"),"VEC2",2,false,q.uv)||q.uv.count!=q.pos.count))return fail("invalid collision UVs");for(const auto& [key,value]:at.object)if(key!="POSITION"&&key!="NORMAL"&&key!="TEXCOORD_0")return fail("unsupported collision attributes");}
        vertices+=q.pos.count;triangles+=q.idx.count/3;
        // a double-sided material draws a mirrored back copy of its faces: they count twice toward the limits
        size_t sides=1;
        if(const auto& all=doc.get("materials");!collision&&all.type==Value::Array&&!all.array.empty()&&integer(p.get("material"),0,all.array.size()-1)){const auto& d=all.array[size_t(p.get("material").number)].get("doubleSided");if(d.type==Value::Bool&&d.boolean)sides=2;}
        drawn_vertices+=sides*q.pos.count;drawn_triangles+=sides*(q.idx.count/3);
        if(collision&&(vertices>4096||triangles>4096))return fail("collision exceeds vertex/triangle limits");
        if(!collision&&(drawn_vertices>kMaxVertices||drawn_triangles>kMaxTriangles))return fail("the visible mesh has more than 4096 vertices or 8192 triangles (all primitives together, double-sided faces twice); indices exceed the limit");
        for(const auto& pair:{std::pair{q.pos,size_t(3)},std::pair{q.nrm,size_t(3)},std::pair{q.uv,size_t(2)}})for(size_t i=0;i<pair.first.count;++i)for(size_t k=0;k<pair.second;++k){const auto& a=pair.first;const uint32_t bits=le32(b.data()+a.offset+i*a.stride+k*4);float f;std::memcpy(&f,&bits,4);if(!std::isfinite(f))return fail("nonfinite model coordinate");}
        for(size_t i=0;i<q.idx.count;++i){const auto* r=b.data()+q.idx.offset+i*q.idx.stride;const uint32_t n=q.idx.component==5121?uint32_t(r[0]):q.idx.component==5123?uint32_t(r[0])|(uint32_t(r[1])<<8):le32(r);if(n>=q.pos.count)return fail("model index outside vertex buffer");}
        prims.push_back(q);
    }
    const auto& nodes=doc.get("nodes");if(nodes.type!=Value::Array||nodes.array.empty()||nodes.array.size()>256)return fail("invalid static model nodes");
    auto vector_ok=[](const Value& v,size_t n){if(v.type!=Value::Array||v.array.size()!=n)return false;for(const auto& x:v.array)if(x.type!=Value::Number||!std::isfinite(x.number))return false;return true;};
    std::vector<unsigned> parents(nodes.array.size()),visited(nodes.array.size());unsigned meshes_seen=0;
    for(const auto& node:nodes.array){
        if(node.type!=Value::Object)return fail("invalid static node");
        for(const char* key:{"skin","weights","extensions"})if(node.get(key).type!=Value::Null)return fail("static node cannot be skinned or extended");
        if(node.get("mesh").type!=Value::Null){if(!integer(node.get("mesh"),0,0))return fail("unknown node mesh");++meshes_seen;}
        if(node.get("matrix").type!=Value::Null){if(!vector_ok(node.get("matrix"),16)||node.get("translation").type!=Value::Null||node.get("rotation").type!=Value::Null||node.get("scale").type!=Value::Null)return fail("invalid node matrix");}
        for(const auto& pair:{std::pair{"translation",size_t(3)},std::pair{"rotation",size_t(4)},std::pair{"scale",size_t(3)}})if(node.get(pair.first).type!=Value::Null&&!vector_ok(node.get(pair.first),pair.second))return fail("invalid node transform");
        const auto& children=node.get("children");if(children.type!=Value::Null){if(children.type!=Value::Array)return fail("invalid node children");for(const auto& child:children.array){if(!integer(child,0,nodes.array.size()-1)||++parents[size_t(child.number)]>1)return fail("invalid or multiply parented node");}}
    }
    if(meshes_seen!=1)return fail("static model needs exactly one mesh instance");
    std::function<bool(size_t)> visit=[&](size_t n){if(visited[n]==1)return false;if(visited[n]==2)return true;visited[n]=1;for(const auto& child:nodes.array[n].get("children").array)if(!visit(size_t(child.number)))return false;visited[n]=2;return true;};
    for(size_t n=0;n<nodes.array.size();++n)if(!visit(n))return fail("cyclic node hierarchy");
    const auto& scenes=doc.get("scenes");if(scenes.type!=Value::Array||scenes.array.size()!=1||(doc.get("scene").type!=Value::Null&&!integer(doc.get("scene"),0,0)))return fail("model needs a default scene");
    const auto& roots=scenes.array[0].get("nodes");if(roots.type!=Value::Array||roots.array.empty())return fail("scene needs nodes");
    std::vector<bool> reached(nodes.array.size());std::function<bool(size_t)> reach=[&](size_t n){if(reached[n])return false;reached[n]=true;for(const auto& child:nodes.array[n].get("children").array)if(!reach(size_t(child.number)))return false;return true;};
    for(const auto& root:roots.array)if(!integer(root,0,nodes.array.size()-1)||parents[size_t(root.number)]||!reach(size_t(root.number)))return fail("invalid scene root");
    for(size_t n=0;n<nodes.array.size();++n)if(nodes.array[n].get("mesh").type!=Value::Null&&!reached[n])return fail("mesh is outside default scene");
    using Matrix=std::array<double,16>;std::vector<Matrix> local(nodes.array.size());
    auto identity=[](){Matrix m{};for(size_t r=0;r<4;++r)m[r*4+r]=1;return m;};
    for(size_t n=0;n<nodes.array.size();++n){const auto& node=nodes.array[n];Matrix m=identity();
        if(node.get("matrix").type!=Value::Null){const auto& a=node.get("matrix").array;for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c)m[r*4+c]=a[c*4+r].number;if(m[12]!=0||m[13]!=0||m[14]!=0||m[15]!=1)return fail("node matrix must be affine");}
        else {double q[4]={0,0,0,1},t[3]={0,0,0},scale[3]={1,1,1};for(size_t k=0;k<4;++k)if(node.get("rotation").type!=Value::Null)q[k]=node.get("rotation").array[k].number;
            double norm=0;for(double v:q)norm+=v*v;if(!std::isfinite(norm)||std::fabs(norm-1)>=1e-4)return fail("node rotation quaternion must be unit length");
            for(size_t k=0;k<3;++k){if(node.get("translation").type!=Value::Null)t[k]=node.get("translation").array[k].number;if(node.get("scale").type!=Value::Null)scale[k]=node.get("scale").array[k].number;}
            const double x=q[0],y=q[1],z=q[2],w=q[3];const double r[9]={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
            for(size_t a=0;a<3;++a){for(size_t c=0;c<3;++c)m[a*4+c]=r[a*3+c]*scale[c];m[a*4+3]=t[a];}}
        for(double x:m)if(!std::isfinite(x))return fail("nonfinite node matrix");local[n]=m;
    }
    Matrix world=identity();std::function<bool(size_t,const Matrix&)> bake=[&](size_t n,const Matrix& parent){Matrix combined{};for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c)for(size_t k=0;k<4;++k)combined[r*4+c]+=parent[r*4+k]*local[n][k*4+c];for(double x:combined)if(!std::isfinite(x))return false;if(nodes.array[n].get("mesh").type!=Value::Null)world=combined;for(const auto& child:nodes.array[n].get("children").array)if(!bake(size_t(child.number),combined))return false;return true;};
    for(const auto& root:roots.array)if(!bake(size_t(root.number),identity()))return fail("nonfinite baked transform");
    const double cof[9]={world[5]*world[10]-world[6]*world[9],world[6]*world[8]-world[4]*world[10],world[4]*world[9]-world[5]*world[8],world[9]*world[2]-world[10]*world[1],world[10]*world[0]-world[8]*world[2],world[8]*world[1]-world[9]*world[0],world[1]*world[6]-world[2]*world[5],world[2]*world[4]-world[0]*world[6],world[0]*world[5]-world[1]*world[4]};
    const double det=world[0]*cof[0]+world[1]*cof[1]+world[2]*cof[2];if(!std::isfinite(det)||std::fabs(det)<=1e-12)return fail("singular model transform");
    auto number=[&](const Access& a,size_t i,size_t k){const uint32_t bits=le32(b.data()+a.offset+i*a.stride+k*4);float f;std::memcpy(&f,&bits,4);return double(f);};
    std::array<float,3> minimum,maximum;minimum.fill(std::numeric_limits<float>::infinity());maximum.fill(-std::numeric_limits<float>::infinity());
    for(const auto& q:prims)for(size_t v=0;v<q.pos.count;++v){double normal_length=0;for(size_t r=0;r<3;++r){double coordinate=world[r*4+3],normal=0;for(size_t k=0;k<3;++k){coordinate+=world[r*4+k]*number(q.pos,v,k);if(!collision)normal+=cof[r*3+k]*number(q.nrm,v,k)/det;}coordinate*=100;if(!std::isfinite(coordinate)||std::fabs(coordinate)>(collision?20000:1000000))return fail("baked model exceeds coordinate range");minimum[r]=std::min(minimum[r],float(coordinate));maximum[r]=std::max(maximum[r],float(coordinate));normal_length+=normal*normal;}if(!collision&&(!std::isfinite(normal_length)||normal_length<=1e-24))return fail("invalid model normal");}
    if(collision){
        for(size_t k=0;k<3;++k)if(double(maximum[k])-minimum[k]>20000)return fail("collision extent exceeds 20000 game units");
        for(const auto& q:prims){
            auto point=[&](size_t i){std::array<float,3> c{};for(size_t r=0;r<3;++r){double value=world[r*4+3];for(size_t k=0;k<3;++k)value+=world[r*4+k]*number(q.pos,i,k);c[r]=float(value*100);}return c;};
            auto index=[&](size_t i){const auto* r=b.data()+q.idx.offset+i*q.idx.stride;return q.idx.component==5121?uint32_t(r[0]):q.idx.component==5123?uint32_t(r[0])|(uint32_t(r[1])<<8):le32(r);};
            for(size_t i=0;i<q.idx.count;i+=3){const auto a=point(index(i)),bb=point(index(i+1)),c=point(index(i+2));double u[3],v[3];for(size_t k=0;k<3;++k){u[k]=double(bb[k])-a[k];v[k]=double(c[k])-a[k];}const double x=u[1]*v[2]-u[2]*v[1],y=u[2]*v[0]-u[0]*v[2],z=u[0]*v[1]-u[1]*v[0];if(!(x*x+y*y+z*z>1e-12))return fail("degenerate collision triangle");}
        }
        info={unsigned(vertices),unsigned(triangles),0,0,0,0,minimum,maximum};return true;
    }
    // Materials: 1 to 16, each with one embedded PNG base colour texture. Several primitives may share one.
    const auto& materials=doc.get("materials");const auto& textures=doc.get("textures");const auto& images=doc.get("images");
    if(materials.type!=Value::Array||materials.array.empty()||textures.type!=Value::Array||textures.array.empty()||images.type!=Value::Array||images.array.empty())return fail("static GLB needs a material with a base colour texture");
    if(materials.array.size()>kMaxMaterials){why="the model has "+std::to_string(materials.array.size())+" materials; at most 16 are supported";return false;}
    std::vector<bool> used_material(materials.array.size()),used_texture(textures.array.size()),used_image(images.array.size());
    for(const auto& p:primitives){if(!integer(p.get("material"),0,materials.array.size()-1))return fail("every primitive needs one of the model's materials");used_material[size_t(p.get("material").number)]=true;}
    unsigned cutouts=0,double_sided=0;
    for(size_t m=0;m<materials.array.size();++m){
        const auto& mat=materials.array[m];const auto label="material "+std::to_string(m)+(mat.get("name").type==Value::String?" ("+mat.get("name").text+")":std::string())+": ";
        auto bad=[&](const char* s){why=label+s;return false;};
        if(mat.type!=Value::Object)return bad("invalid material");
        const auto& colour=mat.get("pbrMetallicRoughness").get("baseColorTexture");
        if(!integer(colour.get("index"),0,textures.array.size()-1)||(colour.get("texCoord").type!=Value::Null&&!integer(colour.get("texCoord"),0,0)))return bad("model needs embedded base colour PNG on UV0");
        const auto& texture=textures.array[size_t(colour.get("index").number)];
        if(!integer(texture.get("source"),0,images.array.size()-1))return bad("model needs embedded base colour PNG on UV0");
        used_texture[size_t(colour.get("index").number)]=true;used_image[size_t(texture.get("source").number)]=true;
        const auto& mode=mat.get("alphaMode");const bool mask=mode.type==Value::String&&mode.text=="MASK";
        if(mode.type==Value::String&&mode.text=="BLEND")return bad("blended transparency (alphaMode BLEND) is not supported; use MASK for cut-out leaves, fences and foliage cards, or OPAQUE");
        if(mode.type!=Value::Null&&!(mode.type==Value::String&&(mode.text=="OPAQUE"||mask)))return bad("unsupported material inputs (alphaMode)");
        const auto& cutoff=mat.get("alphaCutoff");
        if(cutoff.type!=Value::Null&&(!mask||cutoff.type!=Value::Number||!std::isfinite(cutoff.number)||cutoff.number<=0||cutoff.number>=1))return bad("alphaCutoff needs alphaMode MASK and a value between 0 and 1");
        cutouts+=mask;double_sided+=mat.get("doubleSided").type==Value::Bool&&mat.get("doubleSided").boolean;
        const auto& pbr=mat.get("pbrMetallicRoughness");
        if((mat.get("doubleSided").type!=Value::Null&&mat.get("doubleSided").type!=Value::Bool)||pbr.get("metallicRoughnessTexture").type!=Value::Null)return bad("unsupported material inputs");
        if(pbr.get("baseColorFactor").type!=Value::Null){const auto& factor=pbr.get("baseColorFactor");if(factor.type!=Value::Array||factor.array.size()!=4)return bad("unsupported base colour factor");for(const auto& x:factor.array)if(!integer(x,1,1))return bad("unsupported base colour factor (paint the colour into the texture)");}
        for(const char* key:{"normalTexture","occlusionTexture","emissiveTexture","extensions"})if(mat.get(key).type!=Value::Null)return bad("unsupported material texture or extension");
    }
    for(bool used:used_material)if(!used)return fail("every material must be used by a visible primitive");
    for(bool used:used_texture)if(!used)return fail("every texture must be used by a material");
    for(bool used:used_image)if(!used)return fail("every embedded image must be used by a material");
    auto crc=[](const uint8_t* data,size_t size){uint32_t value=0xFFFFFFFFu;for(size_t i=0;i<size;++i){value^=data[i];for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xEDB88320u&uint32_t(-int32_t(value&1)));}return value^0xFFFFFFFFu;};
    std::vector<Image> found;size_t texture_bytes=0;
    for(const auto& image:images.array){
        if(image.get("uri").type!=Value::Null||image.get("mimeType").string()!="image/png")return fail("model needs embedded base colour PNG on UV0");
        size_t off,len,stride;if(!view(image.get("bufferView"),off,len,stride)||stride||len<33)return fail("invalid embedded image");const auto* png=b.data()+base+off;
        if(std::memcmp(png,"\x89PNG\r\n\x1a\n",8)||be32(png+8)!=13||std::memcmp(png+12,"IHDR",4)||png[24]!=8||(png[25]!=2&&png[25]!=6)||png[26]||png[27]||png[28])return fail("texture needs noninterlaced RGB/RGBA8 PNG");
        const auto w=be32(png+16),h=be32(png+20);if(!w||!h||w>kMaxTextureDimension||h>kMaxTextureDimension||w%64||h%8)return fail("model texture exceeds 512 pixels");
        texture_bytes+=size_t(w)*h*4;if(texture_bytes>kMaxTextureBytes)return fail("the textures of one model take more than 1 MiB (for example one 512 x 512, four 256 x 256 or sixteen 128 x 128 textures)");
        bool image_data=false,ended=false;size_t cursor=8;
        while(cursor<len){if(len-cursor<12)return fail("truncated PNG chunk");const size_t count=be32(png+cursor);if(count>len-cursor-12)return fail("PNG chunk outside image");const auto* kind=png+cursor+4;
            if(crc(kind,count+4)!=be32(png+cursor+8+count))return fail("PNG CRC mismatch");
            if(cursor==8){if(std::memcmp(kind,"IHDR",4))return fail("PNG IHDR must be first");}
            else if(!std::memcmp(kind,"IHDR",4))return fail("duplicate PNG IHDR");
            else if(!std::memcmp(kind,"IDAT",4)){image_data=true;}
            else if(!std::memcmp(kind,"IEND",4)){if(count||cursor+12!=len||!image_data)return fail("invalid PNG IEND");ended=true;break;}
            else if(!(kind[0]&32))return fail("unsupported critical PNG chunk");
            cursor+=12+count;
        }
        if(!ended)return fail("incomplete PNG image");
        found.push_back({base+off,len,w,h});
    }
    info={unsigned(vertices),unsigned(triangles),found[0].width,found[0].height,found[0].offset,found[0].size,minimum,maximum};
    info.material_count=unsigned(materials.array.size());info.cutout_count=cutouts;info.double_sided_count=double_sided;info.texture_bytes=texture_bytes;info.images=std::move(found);return true;
}

// Partition original collision-only meshes before reusing the strict rendering
// validator. The projection preserves node transforms and embedded BIN offsets.
inline bool validate_glb(std::span<const uint8_t> b,ModelInfo& info,std::string& why,bool collision_requested=false){
    using mods::json::Value;info={};auto fail=[&](const char* text){why=text;return false;};
    if(b.size()<28||b.size()>kMaxSourceBytes||std::memcmp(b.data(),"glTF",4)||le32(b.data()+4)!=2||le32(b.data()+8)!=b.size())return fail("invalid or oversized GLB 2.0");
    const size_t jl=le32(b.data()+12);if(jl%4||jl>b.size()-28||le32(b.data()+16)!=0x4E4F534A)return fail("invalid GLB JSON chunk");
    const size_t base=28+jl;if(le32(b.data()+20+jl)!=b.size()-base||le32(b.data()+24+jl)!=0x004E4942||(b.size()-base)%4)return fail("invalid GLB BIN chunk");
    Value doc;try{doc=mods::json::parse(std::string(reinterpret_cast<const char*>(b.data()+20),jl));}catch(...){return fail("invalid GLB JSON");}
    const auto& nodes=doc.get("nodes");const auto& meshes=doc.get("meshes");
    if(nodes.type!=Value::Array||nodes.array.empty()||nodes.array.size()>256||meshes.type!=Value::Array||meshes.array.empty()||meshes.array.size()>64)return fail("invalid static scene size");
    std::vector<std::pair<size_t,size_t>> collision,render;std::set<size_t> used;
    auto named=[](const Value& value){return value.get("name").type==Value::String&&value.get("name").text.ends_with("_col");};
    for(size_t i=0;i<nodes.array.size();++i){const auto& node=nodes.array[i];if(node.get("mesh").type==Value::Null)continue;if(!integer(node.get("mesh"),0,meshes.array.size()-1))return fail("node mesh outside input");const size_t mi=size_t(node.get("mesh").number);used.insert(mi);(named(node)||named(meshes.array[mi])?collision:render).emplace_back(i,mi);}
    if(collision.empty()){
        if(!validate_glb_strict(b,info,why))return false;
        // collision from the visible triangles: each visible material's col_* name selects its surface
        if(collision_requested){ModelInfo geometry;if(!validate_glb_strict(b,geometry,why,true)){info={};return false;}for(const auto& material:doc.get("materials").array){const auto name=material.get("name").string();if(name.starts_with("col_")&&!collision_material(name)){info={};return fail("unknown or unsupported collision material");}}}
        return true;
    }
    if(render.size()!=1||used.size()!=meshes.array.size())return fail("one render mesh instance and no unused meshes required");
    std::function<bool(const Value&)> extensions_ok=[&](const Value& value){if(value.type==Value::Object){if(value.get("extensions").type!=Value::Null&&!(value.get("extensions").type==Value::Object&&value.get("extensions").object.empty()))return false;for(const auto& [key,child]:value.object)if(!extensions_ok(child))return false;}else if(value.type==Value::Array)for(const auto& child:value.array)if(!extensions_ok(child))return false;return true;};
    if(!extensions_ok(doc))return fail("GLB extensions are unsupported");
    const auto& materials=doc.get("materials");if(materials.type!=Value::Array||materials.array.empty())return fail("model needs materials");
    auto project=[&](size_t node_index,const Value& mesh){auto clean=doc;for(auto& node:clean["nodes"].array)node.object.erase("mesh");clean["nodes"].array[node_index]["mesh"]=0;clean["meshes"].type=Value::Array;clean["meshes"].array={mesh};return clean;};
    auto packed=[&](const Value& clean){auto text=mods::json::dump(clean);while(text.size()%4)text+=' ';std::vector<uint8_t> out;auto word=[&](uint32_t n){for(unsigned k=0;k<4;++k)out.push_back(uint8_t(n>>(8*k)));};word(0x46546C67);word(2);word(uint32_t(28+text.size()+b.size()-base));word(uint32_t(text.size()));word(0x4E4F534A);out.insert(out.end(),text.begin(),text.end());word(uint32_t(b.size()-base));word(0x004E4942);out.insert(out.end(),b.begin()+base,b.end());return out;};
    // the visible mesh with its own materials (first-use order); collision-only materials are left out
    auto [rn,rm]=render[0];auto render_mesh=meshes.array[rm];const auto& primitives=render_mesh.get("primitives");if(primitives.type!=Value::Array||primitives.array.empty()||primitives.array.size()>kMaxPrimitives)return fail("the visible mesh needs 1 to 32 primitives");
    std::vector<size_t> visible;
    for(auto& p:render_mesh["primitives"].array){const auto& mat=p.get("material");if(!integer(mat,0,materials.array.size()-1))return fail("render material outside input");const size_t material=size_t(mat.number);auto at=std::find(visible.begin(),visible.end(),material);if(at==visible.end())at=visible.insert(visible.end(),material);p["material"]=double(at-visible.begin());}
    auto clean=project(rn,render_mesh);clean["materials"].array.clear();for(size_t material:visible)clean["materials"].array.push_back(materials.array[material]);const auto projected=packed(clean);ModelInfo result;
    if(!validate_glb_strict(projected,result,why))return false;
    // the BIN chunk is unchanged: move every image offset from the projection back into the original file
    const size_t projected_base=28+le32(projected.data()+12);for(auto& image:result.images){if(image.offset<projected_base)return fail("invalid projected image offset");image.offset=image.offset-projected_base+base;}
    result.image_offset=result.images.front().offset;
    size_t vertices=0,triangles=0;std::array<float,3> low,high;low.fill(std::numeric_limits<float>::infinity());high.fill(-std::numeric_limits<float>::infinity());
    for(auto [ni,mi]:collision){const auto& mesh=meshes.array[mi];const auto& ps=mesh.get("primitives");if(mesh.get("weights").type!=Value::Null||ps.type!=Value::Array||ps.array.empty()||ps.array.size()>64)return fail("invalid collision mesh");
        for(const auto& p:ps.array){const auto& material_index=p.get("material");if(!integer(material_index,0,materials.array.size()-1))return fail("collision material outside input");const auto name=materials.array[size_t(material_index.number)].get("name").string();if(!collision_material(name))return fail("unknown or unsupported collision material");
            auto one=mesh;one["primitives"].array={p};const auto geometry=packed(project(ni,one));ModelInfo part;if(!validate_glb_strict(geometry,part,why,true))return false;vertices+=part.vertex_count;triangles+=part.triangle_count;if(vertices>4096||triangles>4096)return fail("collision exceeds vertex/triangle limits");for(size_t k=0;k<3;++k){low[k]=std::min(low[k],part.minimum[k]);high[k]=std::max(high[k],part.maximum[k]);}}
    }
    for(size_t k=0;k<3;++k)if(double(high[k])-low[k]>20000)return fail("collision extent exceeds 20000 game units");info=result;return true;
}
}
