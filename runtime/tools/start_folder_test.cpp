// platform/start_folder.h (issue #266): the game started outside its data folder finds that folder from
// its own location (bin/ in the data folder, a code-mod build, the macOS app's launcher) and keeps
// --data-dir, WWHD_INSTALL_DIR and relative WWHD_ path settings meaning what they meant.
#include "platform/start_folder.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

int main() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
        ("wwhd-start-folder-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(root, ec);
    const fs::path data = root / "Wind Waker HD data";
    fs::create_directories(data / "bin");
    fs::create_directories(data / "code-builds" / "0123456789abcdef" / "bin");
    fs::create_directories(root / "elsewhere");

    // not a data folder yet: no install.json
    assert(!start_folder::is_data_folder(data));
    assert(start_folder::find(data / "bin", "", "").empty());
    std::ofstream(data / "install.json") << "{}";
    assert(start_folder::is_data_folder(data));

    // <data>/bin/wwhd -> <data>
    assert(start_folder::find(data / "bin", "", "") == data);
    // a source build (build/wwhd): no data folder, the working folder stays
    assert(start_folder::find(root / "elsewhere", "", "").empty());
    assert(start_folder::find(fs::path(), "", "").empty());
    // a code-mod build runs with WWHD_INSTALL_DIR=<data>
    assert(start_folder::find(data / "code-builds" / "0123456789abcdef" / "bin", "", data.string()) == data);
    // a WWHD_INSTALL_DIR that is no data folder is not used
    assert(start_folder::find(root / "elsewhere", "", (root / "elsewhere").string()).empty());
    // --data-dir wins
    assert(start_folder::find(data / "bin", (root / "elsewhere").string(), "") == root / "elsewhere");

#ifndef _WIN32  // the app bundle exists on macOS only; its script quotes POSIX paths
    // the macOS app: Contents/MacOS/launch names the data folder (setup.py mac_app's script)
    const fs::path macos = root / "Wind Waker HD.app" / "Contents" / "MacOS";
    fs::create_directories(macos);
    assert(start_folder::find(macos, "", "").empty());
    {
        std::ofstream f(macos / "launch");
        f << "#!/bin/sh\n# written by the Wind Waker HD setup\ncd \"" << data.string()
          << "\" || exit 1\nexec \"$(dirname \"$0\")/wwhd\" --game game --save save \"$@\"\n";
    }
    assert(start_folder::app_launcher_folder(macos) == data);
    assert(start_folder::find(macos, "", "") == data);
    {
        std::ofstream f(macos / "launch");
        f << "#!/bin/sh\ncd \"/tmp/a \\\"quoted\\\" folder\" || exit 1\n";
    }
    assert(start_folder::app_launcher_folder(macos) == fs::path("/tmp/a \"quoted\" folder"));
#endif

    // already in the data folder (the launcher's start): nothing changes
    assert(!start_folder::needs_change(data, data));
    assert(!start_folder::needs_change(data / "bin" / "..", data));
    assert(start_folder::needs_change(root / "elsewhere", data));
    assert(!start_folder::needs_change(root / "elsewhere", fs::path()));

    // relative WWHD_ path settings keep meaning the start folder; plain values stay
    assert(start_folder::path_setting("WWHD_SHADER_CACHE", "build/testcache.bin"));
    assert(start_folder::path_setting("WWHD_STATE_DIR", "states"));
    assert(start_folder::path_setting("WWHD_LOG_FILE", "run.log"));
    assert(!start_folder::path_setting("WWHD_LOG_FILE", "1"));
    assert(!start_folder::path_setting("WWHD_SHADER_CACHE", "0"));
    assert(!start_folder::path_setting("WWHD_SHADER_CACHE", (root / "cache.bin").string()));
    assert(!start_folder::path_setting("WWHD_RENDERER_RUNTIME", "metal"));
    assert(!start_folder::path_setting("WWHD_VK_FASTPATH", "x/y"));
    assert(!start_folder::path_setting("HOME_DIR", "x"));
    assert(!start_folder::path_setting("WWHD_STATE_DIR", ""));
    assert(fs::path(start_folder::absolute_from(root / "elsewhere", "build/testcache.bin")) ==
           root / "elsewhere" / "build" / "testcache.bin");
    const std::string absolute = (root / "x.bin").string();
    assert(start_folder::absolute_from(root / "elsewhere", absolute) == absolute);

    fs::remove_all(root, ec);
    std::puts("start folder: ok");
    return 0;
}
