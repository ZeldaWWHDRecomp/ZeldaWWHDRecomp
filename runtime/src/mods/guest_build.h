// Install-time build bridge shared by runtime startup and synthetic tests.
#pragma once
#include "mod_json.h"
#include "../platform/process.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace guestmods {
struct BuildBridge {
    std::vector<std::string> python, compiler;
    std::string builder, include, cache, zig_cache;
    static std::vector<std::string> arguments(const mods::json::Value& value) {
        if(value.type!=mods::json::Value::Array||value.array.empty())throw std::runtime_error("Invalid guest build tool command");
        std::vector<std::string> out;
        for(const auto& arg:value.array){if(arg.type!=mods::json::Value::String||arg.text.empty()||arg.text.find('\0')!=std::string::npos)
            throw std::runtime_error("Invalid guest build tool argument");out.push_back(arg.text);}
        return out;
    }
    static BuildBridge read(const std::string& path,const std::string& cache) {
        if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>1024*1024)
            throw std::runtime_error("Guest mod build tools are unavailable; run setup again");
        std::ifstream input(path);std::string text{std::istreambuf_iterator<char>(input),{}};
        auto config=mods::json::parse(text);
        const auto& version=config.get("format_version");
        if(version.type!=mods::json::Value::Number||(version.number!=1&&version.number!=2))
            throw std::runtime_error("Unsupported guest build configuration; run setup again");
        BuildBridge b;b.python=arguments(config.get("python"));b.compiler=arguments(config.get("compiler"));
        b.builder=config.get("builder").string();b.include=config.get("include").string();b.cache=cache;
        b.zig_cache=config.get("zig_cache").string();
        if(version.number==2) {
            const auto base=std::filesystem::absolute(path).parent_path();
            auto resolve=[&](std::string& value,bool command=false){
                std::filesystem::path p(value);
                if(!value.empty()&&p.is_relative()&&(!command||p.has_parent_path()))value=(base/p).lexically_normal().string();
            };
            resolve(b.python[0],true);resolve(b.compiler[0],true);
            resolve(b.builder);resolve(b.include);resolve(b.zig_cache);
        }
        if(b.builder.empty()||b.include.empty())throw std::runtime_error("Incomplete guest build configuration; run setup again");
        return b;
    }
    mods::json::Value run(const std::string& package,uint32_t base,bool inspect,const std::string& build="USA") const {
        auto command=python;command.push_back(builder);command.push_back(package);
        command.insert(command.end(),{"--base",std::to_string(base),"--build",build,"--json"});
        if(inspect)command.push_back("--inspect");
        else {
            if(!zig_cache.empty())command.insert(command.end(),{"--zig-cache",zig_cache});
            mods::json::Value cc;cc.type=mods::json::Value::Array;for(const auto& a:compiler)cc.array.emplace_back(a);
            command.insert(command.end(),{"--out",cache,"--include",include,"--cc-json",mods::json::dump(cc)});
        }
        auto process=host::run_process(command);
        if(!process.error.empty())throw std::runtime_error("Guest mod build failed: "+process.error);
        auto end=process.output.find_last_not_of("\r\n");
        if(end==std::string::npos)throw std::runtime_error("Guest mod builder returned no result");
        auto begin=process.output.rfind('\n',end);
        auto result=mods::json::parse(process.output.substr(begin==std::string::npos?0:begin+1,end-(begin==std::string::npos?0:begin+1)+1));
        if(process.code||result.get("ok").type!=mods::json::Value::Bool||!result.get("ok").boolean)
            throw std::runtime_error(result.get("error").string("Guest mod builder failed"));
        return result;
    }
};
} // namespace guestmods
