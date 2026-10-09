#include "mods/catalogue_schema.h"
#include <cassert>
using namespace mods;
static json::Value fixture() {
    return json::parse(R"({"format_version":1,"mods":[{
      "id":"heart-ticker","name":"Heart ticker","description":"Synthetic mod",
      "version":"1.0.0","kind":"guest","authors":["SDK examples"],"licences":["CC0-1.0"],
      "builds":["USA","EU"],"port_versions":{"minimum":"0.2.10","maximum_exclusive":"0.3.0"},
      "downloads":{"all":{"url":"https://example.org/mod.zip","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","size":100}},
      "setup":[{"id":"build","type":"build_guest_mod","title":"Build PowerPC module"}]
    }]})");
}
template<class F> static void refused(F mutate,bool local=false) {
    auto value=fixture();mutate(value);bool bad=false;
    try{catalogue::parse(json::dump(value),local);}catch(const std::exception&){bad=true;}
    assert(bad);
}
int main() {
    auto good=catalogue::parse(json::dump(fixture()));assert(good.entries.size()==1);
    auto& e=good.entries[0];auto version=catalogue::Version::parse("0.2.10");
    assert(e.compatible(version,"EU","windows-x86_64"));
    assert(!e.compatible(version,"EU","android-arm64"));
    assert(!e.compatible(catalogue::Version::parse("0.2.9"),"USA","linux-x86_64"));
    assert(!e.compatible(catalogue::Version::parse("0.3.0"),"USA","linux-x86_64"));
    refused([](auto& v){v["format_version"]=2;});
    refused([](auto& v){v["mods"].array.push_back(v["mods"].array[0]);});
    refused([](auto& v){v["mods"].array[0]["version"]="1.2.3.4";});
    refused([](auto& v){v["mods"].array[0]["id"]="../escape";});
    for(const auto& url:{"http://example.org/mod.zip","https://user:password@example.org/mod.zip",
                         "https:///missing-host","https://example.org/mod.zip#fragment","../escape.zip","C:/mod.zip"})
        refused([&](auto& v){v["mods"].array[0]["downloads"]["all"]["url"]=url;},true);
    refused([](auto& v){v["mods"].array[0]["downloads"]["all"]["size"]=300*1024*1024;});
    refused([](auto& v){v["mods"].array[0]["downloads"]["all"]["sha256"]="abcd";});
    auto local=fixture();local["mods"].array[0]["downloads"]["all"]["url"]="packages/heart-ticker.zip";
    assert(catalogue::parse(json::dump(local),true).entries.size()==1);
    bool rejected=false;try{catalogue::parse(json::dump(local));}catch(...){rejected=true;}assert(rejected);
    for(const auto& path:{"../tool","/tool","tools/../../tool","tools\\tool","tools//tool","tools/tool.","C:tool"})
        assert(!catalogue::relative_file(path));
    assert(catalogue::relative_file("tools/build_maps.py"));
    refused([](auto& v){v["mods"].array[0]["setup"].array[0]["type"]="shell";});
    refused([](auto& v){v["mods"].array[0]["kind"]="settings";});
}
