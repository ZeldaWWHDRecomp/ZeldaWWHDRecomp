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
