#include <algorithm>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>
#include "mods/actor_model_cache.h"
#include "mods/mod_json.h"
#include "mods/mod_hash.h"
#include "static_model_fixture.h"
static std::vector<uint8_t> collision_fixture(){
    std::vector<uint8_t> b(84);
    auto u32=[&](size_t p,uint32_t v){for(unsigned k=0;k<4;++k)b[p+k]=uint8_t(v>>(24-8*k));};
    auto f32=[&](size_t p,float v){u32(p,std::bit_cast<uint32_t>(v));};
    u32(0,0x57434f4c);u32(4,1);u32(8,3);u32(12,1);f32(28,100);f32(36,100);
    f32(52,100);f32(72,100);b[79]=2;b[81]=1;return b;
}
// The largest valid body: 8192 vertices (a 65 x 65 grid, the rest repeat its corner) and 8192 triangles,
// 163880 bytes (story_world_collision_mesh.h MaxWcolBytes), well past the old 100000-byte sidecar cap.
static std::vector<uint8_t> full_collision_fixture(){
    namespace m=story_world_collision::mesh;std::vector<uint8_t> b(m::MaxWcolBytes);
    auto u32=[&](size_t p,uint32_t v){for(unsigned k=0;k<4;++k)b[p+k]=uint8_t(v>>(24-8*k));};
    auto f32=[&](size_t p,float v){u32(p,std::bit_cast<uint32_t>(v));};
    auto u16=[&](size_t p,uint16_t v){b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);};
    u32(0,0x57434f4c);u32(4,1);u32(8,m::MaxVertices);u32(12,m::MaxTriangles);f32(28,6400);f32(36,6400);
    for(uint32_t i=0;i<m::MaxVertices;++i){const uint32_t g=std::min<uint32_t>(i,65*65-1);f32(40+12*i,float(g%65)*100);f32(40+12*i+8,float(g/65)*100);}
    size_t p=40+12*m::MaxVertices;for(uint16_t j=0;j<64;++j)for(uint16_t i=0;i<64;++i){const uint16_t a=uint16_t(j*65+i);for(auto t:{std::array<uint16_t,3>{a,uint16_t(a+65),uint16_t(a+1)},std::array<uint16_t,3>{uint16_t(a+1),uint16_t(a+65),uint16_t(a+66)}}){for(unsigned k=0;k<3;++k)u16(p+2*k,t[k]);u16(p+6,1);p+=8;}}
    return b;
}
int main(int argc,char** argv){
    namespace fs=std::filesystem;
    if(argc>1&&std::string(argv[1])=="--converter"){
        std::string output,identity,collision_output;for(int i=2;i+1<argc;++i){if(std::string(argv[i])=="--out")output=argv[i+1];if(std::string(argv[i])=="--identity")identity=argv[i+1];if(std::string(argv[i])=="--collision-out")collision_output=argv[i+1];}
        if(std::string(argv[2])=="fail")return 1;
        // a Python 3.13+ traceback's last line, coloured (issue #280: the Windows release's tools could not import their siblings)
        if(std::string(argv[2])=="fail_colour"){std::cout<<"Traceback (most recent call last):\n  File \"actor_model.py\"\n\x1b[1;35mModuleNotFoundError\x1b[0m: \x1b[35mNo module named 'notebook_model'\x1b[0m\n";return 1;}
        if(std::string(argv[2])=="reuse"&&!fs::is_regular_file(output))return 2;
        if(std::string(argv[2])!="missing"&&std::string(argv[2])!="reuse"){std::ofstream file(output,std::ios::binary);file.write("Yaz0synthetic-test",18);}
        mods::json::Value result;
        for(int i=2;i<argc;++i)if(std::string(argv[i])=="--shoreline"&&std::string(argv[2])!="wrong_mode"){result["shoreline"]=true;result["shoreline_material"]="foam";}
        for(int i=2;i+1<argc;++i)if(std::string(argv[i])=="--shoreline-material"&&std::string(argv[2])!="wrong_material")result["shoreline_material"]=argv[i+1];
        if(std::string(argv[2])!="triangles_missing")result["triangles"]=std::string(argv[2])=="triangles_wrong"?13:12;
        if(std::string(argv[2])=="triangles_fraction")result["triangles"]=12.5;
        if(std::string(argv[2])=="triangles_string")result["triangles"]="12";
        if(!collision_output.empty()){
            auto bytes=std::string(argv[2]).starts_with("collision_full")?full_collision_fixture():collision_fixture();if(std::string(argv[2])=="collision_corrupt")bytes[0]=0;if(std::string(argv[2])=="collision_full_oversize")bytes.push_back(0);
            if(std::string(argv[2])!="collision_missing"){std::ofstream file(collision_output,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
            result["collision_sha256"]=std::string(argv[2])=="collision_digest"?"wrong":mods::hash::sha256_text(std::string(bytes.begin(),bytes.end()));
        }
        result["identity"]=std::string(argv[2])=="wrong"?"wrong":identity;std::cout<<mods::json::dump(result)<<'\n';return 0;
    }
    const auto root=fs::temp_directory_path()/("wwhd-model-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Clean{fs::path p;~Clean(){std::error_code e;fs::remove_all(p,e);}}clean{root};
    fs::create_directories(root/"package with spaces/models");auto fixture_doc=static_model_fixture::cube_document();fixture_doc["nodes"].array[0]["translation"]=mods::json::parse("[1,2,3]");fixture_doc["nodes"].array[0]["scale"]=mods::json::parse("[2,3,4]");const auto fixture=static_model_fixture::glb(fixture_doc,static_model_fixture::cube_binary());{std::ofstream file(root/"package with spaces/models/own.glb",std::ios::binary);file.write(reinterpret_cast<const char*>(fixture.data()),std::streamsize(fixture.size()));}
    const std::vector<std::string> command{fs::absolute(argv[0]).string(),"--converter"};
    story_notebook::ManifestModel model{"post","models/own.glb"};
    auto prepare=[&](const std::string& mod,const std::string& tool){return actor_model_cache::prepare(mod,model,(root/"package with spaces").string(),(root/"data").string(),command,tool,"game with spaces");};
    const auto first=prepare("owner","ok");assert(first.size()==31&&first.starts_with("mod"));
    const auto guest="/vol/content/Common/Object/"+first+".szs";const auto file=actor_model_cache::archive_for(guest);assert(fs::path(file).lexically_normal()==(root/"data"/"models"/(first+".szs")).lexically_normal());
    story_notebook::WorldPoint minimum,maximum;assert(actor_model_cache::bounds_for(first,minimum,maximum));assert(minimum.x==0&&minimum.y==50&&minimum.z==100&&maximum.x==200&&maximum.y==350&&maximum.z==500);
    assert(!actor_model_cache::bounds_for("missing",minimum,maximum));
    assert(actor_model_cache::triangles_for(first)==12&&actor_model_cache::triangles_for("missing")==0);
    const auto second=prepare("other","ok");assert(second!=first);assert(prepare("owner","reuse")==first);
    for(const auto& path:{first+".szs","/vol/content/Common/Object/../Object/"+first+".szs","/vol/content/Common/object/"+first+".szs",guest+"/tail","/vol/content/Common/Object/"+first+".bfres"})assert(actor_model_cache::archive_for(path).empty());
    auto fails=[&](const std::string& tool){bool threw=false;try{prepare("owner",tool);}catch(const std::exception&){threw=true;}assert(threw&&actor_model_cache::triangles_for(first)==0&&actor_model_cache::archive_for(guest).empty()&&!actor_model_cache::bounds_for(first,minimum,maximum));};
    fails("fail");prepare("owner","ok");fails("wrong");
    // why a model could not be prepared: kept per (mod, model) until it is prepared, one plain line (issue #280)
    {
        prepare("owner","ok");assert(actor_model_cache::failure_for("owner","post").empty()&&!actor_model_cache::preparing("owner","post"));
        fails("fail_colour");
        const auto why=actor_model_cache::failure_for("owner","post");
        assert(why=="Static model conversion failed (ModuleNotFoundError: No module named 'notebook_model'); check the model and player game resources");
        assert(actor_model_cache::failure_for("other","post").empty()&&!actor_model_cache::preparing("owner","post"));
        prepare("owner","ok");assert(actor_model_cache::failure_for("owner","post").empty());
        actor_model_cache::note_failure("owner","kit-rock","a name the kit room doesn't have");assert(actor_model_cache::failure_for("owner","kit-rock")=="a name the kit room doesn't have");
        actor_model_cache::clear_failure("owner","kit-rock");assert(actor_model_cache::failure_for("owner","kit-rock").empty());
        assert(actor_model_cache::plain_reason("\x1b[31mred\x1b[0m\r\n  line\ttwo  ")=="red line two");
        assert(actor_model_cache::plain_reason(std::string(300,'x')).size()==203);
    }
    for(const char* invalid:{"triangles_missing","triangles_wrong","triangles_fraction","triangles_string"}){prepare("owner","ok");fails(invalid);}
    fs::remove(file);fails("missing");
    auto prepare_collision=[&](const std::string& tool){return actor_model_cache::prepare("owner",model,(root/"package with spaces").string(),(root/"data").string(),command,tool,"game with spaces",true);};
    for(const char* invalid:{"collision_corrupt","collision_digest","collision_missing"}){
        assert(prepare_collision("ok")==first);auto mesh=actor_model_cache::collision_for(first);assert(actor_model_cache::triangles_for(first)==12);assert(mesh&&mesh->vertices.size()==3&&mesh->triangles.size()==1&&mesh->triangles[0].indices[1]==2);
        if(std::string(invalid)=="collision_missing")fs::remove(file+".wcol");
        bool rejected=false;try{prepare_collision(invalid);}catch(const std::exception&){rejected=true;}
        assert(rejected&&actor_model_cache::archive_for(guest).empty()&&!actor_model_cache::collision_for(first)&&!actor_model_cache::bounds_for(first,minimum,maximum));
    }
    assert(prepare_collision("ok")==first);assert(actor_model_cache::collision_for(first));
    // a body at the 8192-vertex/8192-triangle limit loads from the cache; one byte more is refused
    {static_assert(story_world_collision::mesh::MaxWcolBytes==163880);assert(prepare_collision("collision_full")==first);auto mesh=actor_model_cache::collision_for(first);
     assert(mesh&&mesh->vertices.size()==8192&&mesh->triangles.size()==8192&&mesh->maximum.x==6400&&mesh->maximum.z==6400);
     bool rejected=false;try{prepare_collision("collision_full_oversize");}catch(const std::exception&){rejected=true;}assert(rejected&&!actor_model_cache::collision_for(first));
     assert(prepare_collision("ok")==first&&actor_model_cache::collision_for(first));}
    prepare("owner","ok");assert(!actor_model_cache::collision_for(first)); // Render-only rebuild cannot retain old collision.
    auto prepare_coast=[&](const char* tool){return actor_model_cache::prepare("owner",model,(root/"package with spaces").string(),(root/"data").string(),command,tool,"game with spaces",false,true);};
    assert(prepare_coast("ok")==first&&actor_model_cache::shoreline_for(first));
    assert(!actor_model_cache::shoreline_for("missing")&&!actor_model_cache::collision_for(first));
    bool wrong_mode=false;try{prepare_coast("wrong_mode");}catch(const std::exception&){wrong_mode=true;}assert(wrong_mode&&!actor_model_cache::shoreline_for(first)&&actor_model_cache::archive_for(guest).empty());
    prepare("owner","ok");assert(!actor_model_cache::shoreline_for(first));
    // a water surface: the shoreline conversion with the still-water material; a converter that answers foam is refused
    auto prepare_water=[&](const char* tool){return actor_model_cache::prepare("owner",model,(root/"package with spaces").string(),(root/"data").string(),command,tool,"game with spaces",false,true,"water");};
    assert(prepare_water("ok")==first&&actor_model_cache::shoreline_for(first)&&actor_model_cache::water_for(first));
    bool wrong_material=false;try{prepare_water("wrong_material");}catch(const std::exception&){wrong_material=true;}assert(wrong_material&&!actor_model_cache::water_for(first));
    assert(prepare_coast("ok")==first&&!actor_model_cache::water_for(first)&&actor_model_cache::material_for(first).empty());
    // a river and a waterfall surface (the editor's water tool): their own materials, said back by the converter
    for(const char* flowing:{"river","waterfall","lava"}){assert(actor_model_cache::prepare("owner",model,(root/"package with spaces").string(),(root/"data").string(),command,"ok","game with spaces",false,true,flowing)==first&&actor_model_cache::material_for(first)==flowing&&!actor_model_cache::water_for(first));}
    bool unknown_material=false;try{actor_model_cache::prepare("owner",model,(root/"package with spaces").string(),(root/"data").string(),command,"ok","game with spaces",false,true,"magma");}catch(const std::exception&){unknown_material=true;}assert(unknown_material);
    bool water_without_shoreline=false;try{actor_model_cache::prepare("owner",model,(root/"package with spaces").string(),(root/"data").string(),command,"ok","game with spaces",false,false,"water");}catch(const std::exception&){water_without_shoreline=true;}assert(water_without_shoreline);
    prepare("owner","ok");assert(!actor_model_cache::shoreline_for(first));
    fails("fail");
    assert(prepare(std::string(64,'a'),"ok").size()==31);
    model.kind="unknown";bool bad_kind_failed=false;try{prepare("owner","ok");}catch(const std::exception&){bad_kind_failed=true;}assert(bad_kind_failed);model.kind="static";
    bool long_owner_failed=false;try{prepare(std::string(65,'a'),"ok");}catch(const std::exception&){long_owner_failed=true;}assert(long_owner_failed);
    model.source="../outside.glb";fails("ok");
    assert(actor_model_cache::archive_for("/vol/content/Common/Object/mod0000000000000000000000000000.szs").empty());
    std::cout<<"actor model cache tests passed\n";
}
