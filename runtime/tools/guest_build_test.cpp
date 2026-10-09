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
        assert(package=="package with spaces & % and $()");
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
    std::cout<<"guest build argument/result bridge passed\n";
}
