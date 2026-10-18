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
int main(int argc,char** argv){
    namespace fs=std::filesystem;
    if(argc>1&&std::string(argv[1])=="--converter"){
        std::string output,identity,collision_output;for(int i=2;i+1<argc;++i){if(std::string(argv[i])=="--out")output=argv[i+1];if(std::string(argv[i])=="--identity")identity=argv[i+1];if(std::string(argv[i])=="--collision-out")collision_output=argv[i+1];}
        if(std::string(argv[2])=="fail")return 1;
        if(std::string(argv[2])=="reuse"&&!fs::is_regular_file(output))return 2;
        if(std::string(argv[2])!="missing"&&std::string(argv[2])!="reuse"){std::ofstream file(output,std::ios::binary);file.write("Yaz0synthetic-test",18);}
        mods::json::Value result;
        for(int i=2;i<argc;++i)if(std::string(argv[i])=="--shoreline"&&std::string(argv[2])!="wrong_mode")result["shoreline"]=true;
        if(std::string(argv[2])!="triangles_missing")result["triangles"]=std::string(argv[2])=="triangles_wrong"?13:12;
        if(std::string(argv[2])=="triangles_fraction")result["triangles"]=12.5;
        if(std::string(argv[2])=="triangles_string")result["triangles"]="12";
        if(!collision_output.empty()){
            auto bytes=collision_fixture();if(std::string(argv[2])=="collision_corrupt")bytes[0]=0;
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
    prepare("owner","ok");assert(!actor_model_cache::collision_for(first)); // Render-only rebuild cannot retain old collision.
    auto prepare_coast=[&](const char* tool){return actor_model_cache::prepare("owner",model,(root/"package with spaces").string(),(root/"data").string(),command,tool,"game with spaces",false,true);};
    assert(prepare_coast("ok")==first&&actor_model_cache::shoreline_for(first));
    assert(!actor_model_cache::shoreline_for("missing")&&!actor_model_cache::collision_for(first));
    bool wrong_mode=false;try{prepare_coast("wrong_mode");}catch(const std::exception&){wrong_mode=true;}assert(wrong_mode&&!actor_model_cache::shoreline_for(first)&&actor_model_cache::archive_for(guest).empty());
    prepare("owner","ok");assert(!actor_model_cache::shoreline_for(first));
    fails("fail");
    assert(prepare(std::string(64,'a'),"ok").size()==31);
    model.kind="unknown";bool bad_kind_failed=false;try{prepare("owner","ok");}catch(const std::exception&){bad_kind_failed=true;}assert(bad_kind_failed);model.kind="static";
    bool long_owner_failed=false;try{prepare(std::string(65,'a'),"ok");}catch(const std::exception&){long_owner_failed=true;}assert(long_owner_failed);
    model.source="../outside.glb";fails("ok");
    assert(actor_model_cache::archive_for("/vol/content/Common/Object/mod0000000000000000000000000000.szs").empty());
    std::cout<<"actor model cache tests passed\n";
}
