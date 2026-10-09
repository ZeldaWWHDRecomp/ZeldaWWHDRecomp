#include "mods/catalogue_setup.h"
#include <cassert>
#include <chrono>

int main() {
    namespace fs=std::filesystem;using namespace mods::catalogue;
    auto root=fs::temp_directory_path()/("wwhd-setup-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"package"/"tools");fs::create_directories(root/"data");
    try {
        auto disc=root/"synthetic disc.iso";
        std::array<uint8_t,32> header{};std::copy_n("GZLE01",6,header.begin());
        header[28]=0xC2;header[29]=0x33;header[30]=0x9F;header[31]=0x3D;
        {std::ofstream out(disc,std::ios::binary);out.write(reinterpret_cast<char*>(header.data()),header.size());}
        assert(valid_game_source("gc_usa",disc)&&!valid_game_source("gc_eur",disc));
        assert(valid_game_source("gc_wind_waker",disc));
        auto regional=root/"regional.iso";
        for(const auto* id:{"GZLP01","GZLJ01","GZLE01","OTHER1"}) {
            std::copy_n(id,6,header.begin());
            {std::ofstream out(regional,std::ios::binary);out.write(reinterpret_cast<char*>(header.data()),header.size());}
            assert(valid_game_source("gc_wind_waker",regional)==(std::string(id)!="OTHER1"));
        }
        fs::create_directories(root/"extracted"/"sys");
        std::copy_n("GZLP01",6,header.begin());
        {std::ofstream out(root/"extracted"/"sys"/"boot.bin",std::ios::binary);out.write(reinterpret_cast<char*>(header.data()),header.size());}
        assert(valid_game_source("gc_wind_waker",root/"extracted"));
        header[28]=0;
        {std::ofstream out(regional,std::ios::binary);out.write(reinterpret_cast<char*>(header.data()),header.size());}
        assert(!valid_game_source("gc_wind_waker",regional));
        auto compressed=root/"synthetic.rvz";
        std::array<unsigned char,0x78> rvz{};
        std::copy_n("RVZ\1",4,rvz.begin());rvz[4]=1;rvz[12+3]=0xDC;rvz[0x4B]=1;
        std::copy_n("GZLE01",6,rvz.begin()+0x58);
        rvz[0x74]=0xC2;rvz[0x75]=0x33;rvz[0x76]=0x9F;rvz[0x77]=0x3D;
        auto write_rvz=[&](){std::ofstream out(compressed,std::ios::binary);out.write(reinterpret_cast<char*>(rvz.data()),rvz.size());};
        write_rvz();assert(valid_game_source("gc_wind_waker",compressed)&&valid_game_source("gc_usa",compressed));
        assert(!valid_game_source("gc_eur",compressed));
        std::copy_n("GZLP01",6,rvz.begin()+0x58);write_rvz();
        assert(valid_game_source("gc_eur",compressed)&&valid_game_source("gc_wind_waker",compressed));
        rvz[8]=2;write_rvz();assert(!valid_game_source("gc_wind_waker",compressed));rvz[8]=0;
        rvz[0x4B]=2;write_rvz();assert(!valid_game_source("gc_wind_waker",compressed));rvz[0x4B]=1;
        {std::ofstream out(compressed,std::ios::binary);out.write(reinterpret_cast<char*>(rvz.data()),0x60);}
        assert(!valid_game_source("gc_wind_waker",compressed));
        Sources any_region;assert(any_region.set("gc_wind_waker",root/"extracted"));
        assert(!any_region.get("gc_wind_waker").empty());
        auto generic_steps=mods::json::parse(R"([{"id":"source","type":"game_path","title":"Choose source","game":"gc_wind_waker"}])");
        assert(steps(generic_steps).front().game=="gc_wind_waker");
        Sources sources;assert(sources.set("gc_usa",disc));
        Sources reused(sources.local_settings());assert(reused.get("gc_usa")==fs::canonical(disc).string());
        {std::ofstream out(root/"package"/"tools"/"prepare");out<<"synthetic";}
        Step tool;tool.type="run_tool";tool.tool="tools/prepare";
        tool.arguments={"--source","{game:gc_usa}","--output={data}/result.bin","literal ; $(value)"};
        tool.outputs={"result.bin"};
        auto args=tool_arguments(tool,reused,root/"package",root/"data");
        assert(args.size()==5&&args[2]==fs::canonical(disc).string()&&args[4]=="literal ; $(value)");
        assert(!outputs_satisfied(tool,root/"data"));
        {std::ofstream out(root/"data"/"result.bin");out<<"synthetic output";}
        assert(outputs_satisfied(tool,root/"data"));
        fs::rename(disc,root/"moved.iso");assert(reused.get("gc_usa").empty());
        bool failed=false;try{tool_arguments(tool,reused,root/"package",root/"data");}catch(...){failed=true;}assert(failed);
        fs::create_directories(root/"wiiu"/"meta");
        {std::ofstream out(root/"wiiu"/"meta"/"meta.xml");out<<"<menu><title_id type=\"hexBinary\">0005000010143600</title_id></menu>";}
        assert(valid_game_source("wiiu_eur",root/"wiiu")&&!valid_game_source("wiiu_jpn",root/"wiiu"));
        fs::create_directories(root/"wiiu"/"code");
        {std::ofstream out(root/"wiiu"/"code"/"app.xml");out<<"<title_id>0005000e10143600</title_id>";}
        assert(!valid_game_source("wiiu_eur",root/"wiiu"));
        Step choice;choice.type="choice";choice.choices={"left","right"};
        assert(option_satisfied(choice,"left")&&!option_satisfied(choice,"other"));
        Step confirm;confirm.type="confirm";
        assert(option_satisfied(confirm,true)&&!option_satisfied(confirm,false)&&!option_satisfied(confirm,"true"));
    }catch(...){fs::remove_all(root);throw;}
    fs::remove_all(root);
}
