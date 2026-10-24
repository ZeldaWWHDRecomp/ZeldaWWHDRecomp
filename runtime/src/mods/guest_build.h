// Install-time build bridge shared by runtime startup and synthetic tests.
#pragma once
#include "../exception_report.h"
#include "mod_json.h"
#include "compiler_notice.h"
#include "../platform/process.h"
#ifdef __ANDROID__
#include "../platform/android_code_mods.h"
#endif
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace guestmods {
// guest-sdk.json paths inside the release (issue #230). An AppImage is mounted at a new
// /tmp/.mount_XXXXXX on every start, and the game runs from the data folder (not from the image), so
// setup stores paths inside the image as "$RELEASE/<path with '/'>" (format_version 3), resolved
// against the running release: $APPDIR, which the AppImage runtime sets for setup and the game alike.
inline constexpr const char* release_marker="$RELEASE/";
inline std::string release_root() {
    const char* dir=std::getenv("APPDIR");
    return dir&&*dir?std::string(dir):std::string();
}
// One stored path: the marker against `release`; a relative path (format 2+, `base` not empty) against
// the configuration's folder; an absolute path inside an AppImage mount (".../.mount_XXXXXX/...", what
// setup stored up to format 2) onto the same place in `release` when that exists, so a setup made from
// an earlier start of the image keeps working without running setup again. `command`: a bare program
// name ("python3") stays a PATH lookup.
inline std::string resolve_path(const std::string& value,const std::filesystem::path& base,const std::string& release,bool command=false) {
    namespace fs=std::filesystem;
    if(value.empty())return value;
    const std::string marker=release_marker;
    if(value.compare(0,marker.size(),marker)==0)
        return release.empty()?value:(fs::path(release)/fs::path(value.substr(marker.size()))).lexically_normal().string();
    const fs::path p(value);
    if(p.is_relative())return base.empty()||(command&&!p.has_parent_path())?value:(base/p).lexically_normal().string();
    if(release.empty())return value;
    fs::path tail;bool mounted=false;
    for(const auto& part:p.relative_path()){
        if(part.string().rfind(".mount_",0)==0){tail.clear();mounted=true;}
        else if(mounted)tail/=part;
    }
    if(!mounted||tail.empty())return value;
    const auto moved=(fs::path(release)/tail).lexically_normal();
    std::error_code error;
    return fs::exists(moved,error)?moved.string():value;
}
// A package's Python preparation tool runs through a bootstrap that puts the tool's folder on sys.path
// (the Windows embeddable Python doesn't, issue #224): `python -B -c tool_bootstrap TOOL ARGS...`.
// tool_bootstrap_in_folder also enters a folder first (`-c ... FOLDER TOOL ARGS...`), for Android, where
// the game cannot start a program in another working directory. tools/android/test_tool_bootstrap.py
// runs both.
inline constexpr const char* tool_bootstrap=
    "import os,runpy,sys;s=sys.argv[1];sys.path.insert(0,os.path.dirname(os.path.abspath(s)));"
    "sys.argv=sys.argv[1:];runpy.run_path(s,run_name='__main__')";
inline constexpr const char* tool_bootstrap_in_folder=
    "import os,runpy,sys;s=os.path.abspath(sys.argv[2]);os.chdir(sys.argv[1]);sys.path.insert(0,os.path.dirname(s));"
    "sys.argv=sys.argv[2:];runpy.run_path(s,run_name='__main__')";
struct BuildBridge {
    std::vector<std::string> python, compiler;
    std::string builder, include, cache, zig_cache;
    static std::vector<std::string> arguments(const mods::json::Value& value) {
        if(value.type!=mods::json::Value::Array||value.array.empty())exception_report::raise("Invalid guest build tool command");
        std::vector<std::string> out;
        for(const auto& arg:value.array){if(arg.type!=mods::json::Value::String||arg.text.empty()||arg.text.find('\0')!=std::string::npos)
            exception_report::raise("Invalid guest build tool argument");out.push_back(arg.text);}
        return out;
    }
    static BuildBridge read(const std::string& path,const std::string& cache,const std::string& release=release_root()) {
        if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>1024*1024)
            exception_report::raise("Guest mod build tools are unavailable; run setup again");
        std::ifstream input(path);std::string text{std::istreambuf_iterator<char>(input),{}};
        auto config=mods::json::parse(text);
        const auto& version=config.get("format_version");
        if(version.type!=mods::json::Value::Number||(version.number!=1&&version.number!=2&&version.number!=3))
            exception_report::raise("Unsupported guest build configuration; run setup again");
        BuildBridge b;b.python=arguments(config.get("python"));b.compiler=arguments(config.get("compiler"));
        b.builder=config.get("builder").string();b.include=config.get("include").string();b.cache=cache;
        b.zig_cache=config.get("zig_cache").string();
        // format 1 stored only absolute paths and program names: relative ones stay as they are
        const auto base=version.number>=2?std::filesystem::absolute(path).parent_path():std::filesystem::path();
        for(auto* command:{&b.python[0],&b.compiler[0]})*command=resolve_path(*command,base,release,true);
        for(auto* value:{&b.builder,&b.include,&b.zig_cache})*value=resolve_path(*value,base,release);
        if(b.builder.empty()||b.include.empty())exception_report::raise("Incomplete guest build configuration; run setup again");
        return b;
    }
    // guest-sdk.json: WWHD_GUEST_BUILD_CONFIG (set at startup next to the release, or by a test), else
    // the working directory's. On Android the app writes the file for its installation the first time
    // a code mod needs it (AndroidGuestTools.java: the APK's Python and compiler, which run from the
    // native library folder); later users in the session read the same path.
    static std::string config_path() {
#ifdef __ANDROID__
        if(!std::getenv("WWHD_GUEST_BUILD_CONFIG"))setenv("WWHD_GUEST_BUILD_CONFIG",host::android::guest_build_tools().c_str(),1);
#endif
        const char* path=std::getenv("WWHD_GUEST_BUILD_CONFIG");
        return path?path:"guest-sdk.json";
    }
    // Which of setup's tools is missing on disk (None: all there). A bare program name ("clang", "python3")
    // is a PATH lookup and counts as there; the compiler only matters for code mods (`compiler`).
    enum class Missing { None, Tools, Python, Compiler };
    Missing missing(bool need_compiler=true) const {
        namespace fs=std::filesystem;
        std::error_code error;
        if(!fs::is_regular_file(builder,error)||!fs::is_directory(include,error))return Missing::Tools;
        auto absent=[&](const std::vector<std::string>& command) {
            const fs::path executable(command.front());
            return executable.has_parent_path()&&!fs::is_regular_file(executable,error);
        };
        if(absent(python))return Missing::Python;
        if(need_compiler&&absent(compiler))return Missing::Compiler;
        return Missing::None;
    }
    // setup's tools from guest-sdk.json, all of them present; `need_compiler`=false for the Python-only tools
    // (stage, model and catalogue builders), which work without the compiler
    static BuildBridge installed(const std::string& cache,bool need_compiler=true) {
        auto b=read(config_path(),cache);
        switch(b.missing(need_compiler)) {
        case Missing::Tools:
            if(b.builder.rfind(release_marker,0)==0||b.include.rfind(release_marker,0)==0)
                exception_report::raise("Guest mod build tools come with the AppImage; start the game from the AppImage");
            exception_report::raise("Guest mod build tools are missing; run setup again from a complete release folder");
        case Missing::Python:exception_report::raise(kPythonMissing);
        case Missing::Compiler:exception_report::raise(kCompilerMissing);
        case Missing::None:break;
        }
        return b;
    }
    // The interpreter command for a tools/story_mods tool the game starts itself (the model, stage, island
    // building and catalogue builders): `python -c tool_bootstrap TOOL ARGS...`, so the
    // tool's own folder is on sys.path under every Python. The Windows release's embeddable Python leaves it
    // out (its python314._pth lists only its own folders): every own model of the world editor failed there
    // with "No module named 'notebook_model'" (issue #280), as package tools did before (issue #224).
    std::vector<std::string> tool_python() const {
        auto command=python;
        command.insert(command.end(),{"-c",tool_bootstrap});
        return command;
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
        return invoke(command);
    }
    mods::json::Value check_cached(const mods::json::Value& requests,const std::string& build="USA") const {
        auto command=python;command.push_back(builder);
        mods::json::Value cc;cc.type=mods::json::Value::Array;for(const auto& a:compiler)cc.array.emplace_back(a);
        command.insert(command.end(),{"--check-cache-json",mods::json::dump(requests),"--build",build,
            "--out",cache,"--include",include,"--cc-json",mods::json::dump(cc),"--json"});
        if(!zig_cache.empty())command.insert(command.end(),{"--zig-cache",zig_cache});
        return invoke(command);
    }
    static mods::json::Value invoke(const std::vector<std::string>& command) {
        auto process=host::run_process(command);
        if(!process.error.empty())exception_report::raise("Guest mod build failed: "+process.error);
        auto end=process.output.find_last_not_of("\r\n");
        if(end==std::string::npos)exception_report::raise("Guest mod builder returned no result");
        auto begin=process.output.rfind('\n',end);
        auto result=mods::json::parse(process.output.substr(begin==std::string::npos?0:begin+1,end-(begin==std::string::npos?0:begin+1)+1));
        if(process.code||result.get("ok").type!=mods::json::Value::Bool||!result.get("ok").boolean)
            exception_report::raise(result.get("error").string("Guest mod builder failed"));
        return result;
    }
};
} // namespace guestmods
