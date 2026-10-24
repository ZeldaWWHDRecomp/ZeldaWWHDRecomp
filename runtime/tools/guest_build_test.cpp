#include "mods/guest_build.h"
#include <cassert>
#include <chrono>
#include <iostream>
using mods::json::Value;
int main(int argc,char** argv) {
    if(argc>1&&std::string(argv[1])=="--builder") {
        assert(argc>=7);
        std::string package=argv[3];
        if(package=="failure"){std::cout<<"compiler diagnostics\n{\"ok\":false,\"error\":\"compile failed\"}\n";return 1;}
        if(package=="bad-json"){std::cout<<"not JSON\n";return 0;}
        bool checking=package=="--check-cache-json";
        if(checking) {
            auto requests=mods::json::parse(argv[4]);
            assert(requests.array.size()==1);
            assert(requests.array[0].get("package").string()=="package with spaces & % and $()");
            assert(requests.array[0].get("base").number==0x7F000000);
        } else assert(package=="package with spaces & % and $()");
        bool inspect=false,cc=false,zig=false,build=false;
        for(int i=4;i<argc;++i){
            if(std::string(argv[i])=="--build"){assert(i+1<argc&&std::string(argv[++i])=="EU");build=true;}
            if(std::string(argv[i])=="--inspect")inspect=true;
            if(std::string(argv[i])=="--zig-cache"){assert(i+1<argc&&std::string(argv[++i])=="zig cache with spaces");zig=true;}
            if(std::string(argv[i])=="--cc-json"){
                assert(i+1<argc);auto args=mods::json::parse(argv[++i]);
                assert(args.array.size()==3&&args.array[0].text=="compiler with spaces"&&args.array[2].text=="quoted \\\"arg");cc=true;
            }
        }
        assert(inspect!=cc&&zig==!inspect&&build);
        if(checking){std::cout<<"diagnostic line\n{\"ok\":true,\"valid\":[\"fixture\"]}\n";return 0;}
        std::cout<<"diagnostic line\n{\"ok\":true,\"allocation_size\":65536,\"module\":\"module path\"}\n";
        return 0;
    }
    guestmods::BuildBridge b;
    b.python={std::filesystem::absolute(argv[0]).string(),"--builder"};
    b.compiler={"compiler with spaces","-target","quoted \\\"arg"};
    b.zig_cache="zig cache with spaces";
    b.builder="builder with spaces";b.include="headers with spaces";b.cache="cache with spaces";
    assert(b.run("package with spaces & % and $()",0x7F000000,true,"EU").get("allocation_size").number==65536);
    assert(b.run("package with spaces & % and $()",0x7F000000,false,"EU").get("module").text=="module path");
    Value requests;requests.type=Value::Array;Value request;
    request["id"]="fixture";request["package"]="package with spaces & % and $()";
    request["base"]=double(0x7F000000);request["module"]="module path";requests.array.push_back(request);
    assert(b.check_cached(requests,"EU").get("valid").array[0].string()=="fixture");
    bool failed=false;
    try{b.run("failure",0x7F000000,false);}catch(const std::exception& e){failed=std::string(e.what())=="compile failed";}
    assert(failed);failed=false;
    try{b.run("bad-json",0x7F000000,false);}catch(const std::exception&){failed=true;}
    assert(failed);
    namespace fs=std::filesystem;
    const auto fixture=fs::current_path()/("guest-build-config-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(fixture/"old");
    struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{fixture};
    {std::ofstream out(fixture/"old"/"guest-sdk.json");
     out<<R"({"format_version":2,"python":["../python/python","-B"],"compiler":["./toolchain/zig","cc"],"builder":"../tools/build_guest_mod.py","include":"../sdk/include","zig_cache":"./toolchain/zig-cache"})";}
    fs::rename(fixture/"old",fixture/"moved");
    auto relocated=guestmods::BuildBridge::read((fixture/"moved"/"guest-sdk.json").string(),"cache");
    assert(fs::path(relocated.python[0])==fixture/"python"/"python");
    assert(relocated.python[1]=="-B"&&relocated.compiler[1]=="cc");
    assert(fs::path(relocated.compiler[0])==fixture/"moved"/"toolchain"/"zig");
    assert(fs::path(relocated.builder)==fixture/"tools"/"build_guest_mod.py");
    assert(fs::path(relocated.include)==fixture/"sdk"/"include");
    assert(fs::path(relocated.zig_cache)==fixture/"moved"/"toolchain"/"zig-cache");
    {std::ofstream out(fixture/"legacy.json");
     out<<R"({"format_version":1,"python":["python3"],"compiler":["clang"],"builder":"builder","include":"headers"})";}
    auto legacy=guestmods::BuildBridge::read((fixture/"legacy.json").string(),"cache");
    assert(legacy.python[0]=="python3"&&legacy.builder=="builder");
    {std::ofstream out(fixture/"bad.json");out<<R"({"format_version":999})";}
    failed=false;try{guestmods::BuildBridge::read((fixture/"bad.json").string(),"cache");}catch(const std::exception&){failed=true;}
    assert(failed);
    // AppImage (issue #230): the image is mounted at a new /tmp/.mount_XXXXXX on every start. A format 3
    // configuration names its tools as "$RELEASE/..."; a format 2 one (setup before the fix) has the
    // absolute paths of the mount it was written from, mapped onto the running release.
    const auto first=fixture/".mount_AAA",second=fixture/".mount_BBB",data=fixture/"data";
    fs::create_directories(first/"tools"/"guestmod");fs::create_directories(first/"sdk"/"include");
    fs::create_directories(first/"tools"/"installer");fs::create_directories(data/"toolchain");
    std::ofstream(first/"tools"/"guestmod"/"build_guest_mod.py")<<"";
    std::ofstream(first/"tools"/"installer"/"setup.py")<<"";
    std::ofstream(data/"toolchain"/"zig")<<"";
    auto json_path=[](const fs::path& p){auto s=p.generic_string();std::string out;for(char c:s){if(c=='\\'||c=='"')out+='\\';out+=c;}return out;};
    {std::ofstream out(data/"stale.json");
     out<<"{\"format_version\":2,\"python\":[\"python3\"],\"compiler\":[\""<<json_path(data/"toolchain"/"zig")<<"\",\"cc\"],"
        <<"\"builder\":\""<<json_path(first/"tools"/"guestmod"/"build_guest_mod.py")<<"\","
        <<"\"include\":\""<<json_path(first/"sdk"/"include")<<"\","
        <<"\"missing\":\""<<json_path(first/"tools"/"gone.py")<<"\"}";}
    {std::ofstream out(data/"marker.json");
     out<<R"({"format_version":3,"python":["python3"],"compiler":["$RELEASE/tools/guestmod/build_guest_mod.py","cc"],)"
        <<R"("builder":"$RELEASE/tools/guestmod/build_guest_mod.py","include":"$RELEASE/sdk/include","zig_cache":"./toolchain/zig-cache"})";}
    fs::rename(first,second);
    for(const auto* name:{"stale.json","marker.json"}) {
        auto moved=guestmods::BuildBridge::read((data/name).string(),"cache",second.string());
        assert(fs::path(moved.builder)==(second/"tools"/"guestmod"/"build_guest_mod.py").lexically_normal());
        assert(fs::path(moved.include)==(second/"sdk"/"include").lexically_normal());
        assert(moved.python[0]=="python3"&&moved.compiler[1]=="cc");
        assert(fs::is_regular_file(moved.builder)&&fs::is_directory(moved.include));
    }
    assert(fs::path(guestmods::BuildBridge::read((data/"stale.json").string(),"cache",second.string()).compiler[0])==data/"toolchain"/"zig");
    assert(fs::path(guestmods::BuildBridge::read((data/"marker.json").string(),"cache",second.string()).zig_cache)==(data/"toolchain"/"zig-cache").lexically_normal());
    // a path that is gone from the running release too, or no release to resolve against: left as stored
    const auto gone=(first/"tools"/"gone.py").string();
    assert(guestmods::resolve_path(gone,data,second.string())==gone);
    assert(guestmods::resolve_path(gone,data,"")==gone);
    assert(guestmods::BuildBridge::read((data/"marker.json").string(),"cache","").builder=="$RELEASE/tools/guestmod/build_guest_mod.py");
    assert(guestmods::resolve_path("$RELEASE/tools/installer/setup.py",data,second.string())==(second/"tools"/"installer"/"setup.py").lexically_normal().string());
    // the release folder is the running AppImage's $APPDIR
#ifdef _WIN32
    _putenv_s("APPDIR",second.string().c_str());
#else
    setenv("APPDIR",second.string().c_str(),1);
#endif
    assert(fs::path(guestmods::release_root())==second);
    assert(fs::path(guestmods::BuildBridge::read((data/"stale.json").string(),"cache").builder)==(second/"tools"/"guestmod"/"build_guest_mod.py").lexically_normal());
    // issue #266: setup's compiler (toolchain/) deleted. Code mods report it in plain words; the Python-only
    // tools (stage, model, catalogue builders) don't need it.
    {
        const auto rel=fixture/"release";
        fs::create_directories(rel/"tools"/"guestmod");fs::create_directories(rel/"sdk"/"include");
        fs::create_directories(rel/"python");fs::create_directories(rel/"toolchain");
        std::ofstream(rel/"tools"/"guestmod"/"build_guest_mod.py")<<"";
        std::ofstream(rel/"python"/"python3")<<"";std::ofstream(rel/"toolchain"/"zig")<<"";
        {std::ofstream out(rel/"guest-sdk.json");
         out<<R"({"format_version":2,"python":["./python/python3"],"compiler":["./toolchain/zig","cc"],"builder":"./tools/guestmod/build_guest_mod.py","include":"./sdk/include"})";}
        const auto config=(rel/"guest-sdk.json").string();
#ifdef _WIN32
        _putenv_s("WWHD_GUEST_BUILD_CONFIG",config.c_str());
#else
        setenv("WWHD_GUEST_BUILD_CONFIG",config.c_str(),1);
#endif
        using Missing=guestmods::BuildBridge::Missing;
        assert(guestmods::BuildBridge::read(config,"cache","").missing()==Missing::None);
        assert(guestmods::BuildBridge::installed("cache").compiler[1]=="cc");
        fs::remove(rel/"toolchain"/"zig");
        const auto bridge=guestmods::BuildBridge::read(config,"cache","");
        assert(bridge.missing()==Missing::Compiler);
        assert(bridge.missing(false)==Missing::None);
        std::string why;
        try{guestmods::BuildBridge::installed("cache");}catch(const std::exception& e){why=e.what();}
        assert(why==guestmods::kCompilerMissing);
        assert(fs::path(guestmods::BuildBridge::installed(std::string(),false).python[0])==rel/"python"/"python3");
        fs::remove(rel/"python"/"python3");
        assert(guestmods::BuildBridge::read(config,"cache","").missing(false)==Missing::Python);
        why.clear();
        try{guestmods::BuildBridge::installed(std::string(),false);}catch(const std::exception& e){why=e.what();}
        assert(why==guestmods::kPythonMissing);
        fs::remove(rel/"tools"/"guestmod"/"build_guest_mod.py");
        assert(guestmods::BuildBridge::read(config,"cache","").missing()==Missing::Tools);
        // program names are PATH lookups: not reported missing
        {std::ofstream out(rel/"names.json");
         out<<R"({"format_version":1,"python":["python3"],"compiler":["clang"],"builder":")"<<json_path(rel/"sdk"/"include"/"x.py")
            <<R"(","include":")"<<json_path(rel/"sdk"/"include")<<R"("})";}
        std::ofstream(rel/"sdk"/"include"/"x.py")<<"";
        assert(guestmods::BuildBridge::read((rel/"names.json").string(),"cache","").missing()==Missing::None);
    }
    // the notice: once per start, naming the mods the missing compiler stops
    assert(guestmods::compiler_notice({}).empty());
    assert(guestmods::compiler_notice({"Story Notebook"})==
           "Story Notebook needs the mod compiler, which is missing. Run setup and choose Repair to install it again.");
    assert(guestmods::compiler_notice({"Story Notebook","Call of the Sky"})==
           "Story Notebook and Call of the Sky need the mod compiler, which is missing. Run setup and choose Repair to install it again.");
    assert(guestmods::compiler_notice({"A","B","C"}).rfind("A and 2 other mods need the mod compiler",0)==0);
    struct View {std::string id,name,kind,reason;bool enabled=false,active=false;};
    std::vector<View> views={
        {"story-notebook","Story Notebook","guest",guestmods::kCompilerMissing,true,false},
        {"off","Switched off","guest",guestmods::kCompilerMissing,false,false},       // not chosen: not named
        {"other","Other problem","guest","Package changed; reinstall and confirm it again",true,false},
        {"running","Running","guest","",true,true},
        {"textures","Textures","content",guestmods::kCompilerMissing,true,false},     // not a code mod
        {"no-name","","guest",guestmods::kCompilerMissing,true,false}};
    assert(guestmods::compiler_notice_for(views)==
           "Story Notebook and no-name need the mod compiler, which is missing. Run setup and choose Repair to install it again.");
    views.clear();
    assert(guestmods::compiler_notice_for(views).empty());
    std::cout<<"guest build argument/result bridge passed\n";
}
