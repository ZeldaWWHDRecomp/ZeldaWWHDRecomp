// SPDX-License-Identifier: MPL-2.0
// Link pose icons in the port (link_poses.h): the option, the game's 3D pack and the export next to the
// touch buttons' other art (context_touch.cpp). The pictures are made from the file the game itself
// opened (a content mod's pack included) and written into the exporting folder only (the app cache on
// Android); nothing is kept in the repository or shipped.
//
// Option touchLinkPoses (settings.ini; WWHD_LINK_POSES=0|1 for tests, which also keeps it from being saved):
// on by default. Off: the buttons show the game's words as before (the pictures stay exported, so turning it
// back on shows them at once).
#include "link_poses.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "../overlay/hostui.h"
#include "../runtime.h"

namespace link_poses {
namespace {
std::atomic<int> g_enabled{-1};
std::mutex g_mu;
std::string g_pack3d;  // host path of the 3D pack the game opened

bool lower_is(const std::string& name, const char* want) {
    if (name.size() != strlen(want)) return false;
    for (size_t i = 0; i < name.size(); i++)
        if (char(tolower(static_cast<unsigned char>(name[i]))) != want[i]) return false;
    return true;
}
bool write_file(const std::filesystem::path& p, const Bytes& data) {
    const std::filesystem::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}
}  // namespace

bool enabled() {
    int v = g_enabled.load(std::memory_order_relaxed);
    if (v >= 0) return v != 0;
    v = 1;
    std::string s;
    if (const char* e = getenv("WWHD_LINK_POSES"); e && *e) v = atoi(e) != 0;
    else if (hostui::get("touchLinkPoses", s) && !s.empty()) v = atoi(s.c_str()) != 0;
    g_enabled.store(v, std::memory_order_relaxed);
    return v != 0;
}
void set_enabled(bool on) {
    g_enabled.store(on ? 1 : 0, std::memory_order_relaxed);
    if (!getenv("WWHD_LINK_POSES")) hostui::set("touchLinkPoses", on ? "1" : "0");
}

void note_open(const std::string& guest_path, const std::string& host_path) {
    const size_t slash = guest_path.find_last_of('/');
    const std::string name = slash == std::string::npos ? guest_path : guest_path.substr(slash + 1);
    if (!lower_is(name, "permanent_3d.pack")) return;
    std::lock_guard<std::mutex> lk(g_mu);
    g_pack3d = host_path;
}

int export_to(const std::string& dir, bool (*encode)(uint32_t w, uint32_t h, const uint8_t* rgba, Bytes& png)) {
    std::string path;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        path = g_pack3d;
    }
    if (path.empty()) return 0;
    Bytes pack;
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            LOG("[link-poses] %s can't be read; the buttons keep their words", path.c_str());
            return -1;
        }
        pack.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    Bytes link, anim;
    std::string why;
    std::shared_ptr<const Model> model;
    if (read_pack(pack, link, anim, why)) model = load(link, why);
    if (!model) {
        LOG("[link-poses] Link's model not read (%s); the buttons keep their words", why.c_str());
        return -1;
    }
    pack.clear(), pack.shrink_to_fit();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    int written = 0;
    for (const PoseSpec& p : kPoses) {
        Bytes rgba, png;
        if (!render(*model, anim, p, rgba, why)) {
            LOG("[link-poses] pose %02X (%s): %s", p.action, p.anim, why.c_str());
            continue;
        }
        if (encode(uint32_t(kIconSize), uint32_t(kIconSize), rgba.data(), png) && write_file(std::filesystem::path(dir) / file_for(p.action), png))
            written++;
    }
    LOG("[link-poses] %d of %zu Link poses drawn into %s", written, sizeof kPoses / sizeof kPoses[0], dir.c_str());
    return written > 0 ? written : -1;
}

}  // namespace link_poses
