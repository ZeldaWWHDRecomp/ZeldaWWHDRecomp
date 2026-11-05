// Link's name on the save file being played (player_name.h).
#include "player_name.h"

#include <cstring>
#include <mutex>

#include "guest_addr.h"
#include "long_name.h"
#include "../gfx/renderer.h"
#include "runtime.h"
#include "story_lyt_text_markup.h"  // to_utf8
#include "../console_language.h"
#include "../game_name_text.h"
#include "../input.h"
#include "../savestate.h"
#include "../overlay/text_entry.h"

namespace player_name {
namespace {

const uint32_t kSaveInfoPtr = GD(0x101F84DC);   // dComIfGs save area
constexpr uint32_t kDataNum = 0x20 + 0x1290;     // dSv_info_c::mDataNum: the file being played (0..2)
constexpr uint32_t kCardInfo = 0x12C0;           // SaveMgr's card info (HD per-file data)
const uint32_t fn_get_card_name = GC(0x02720154), fn_set_card_name = GC(0x02726668);
using game_name::kRefused;  // see ask()

std::mutex g_mu;
bool g_pending = false;  // an accepted name, not yet put in (g_mu)
std::u16string g_name;

uint32_t save_info() {
    const uint32_t sv = ld32(kSaveInfoPtr);
    return (sv & 3) == 0 && sv >= mem::kMem2Start && sv < mem::kMem2End - kCardInfo - 0x100 ? sv : 0;
}
int slot(uint32_t sv) {
    const int s = sv ? int(ld8(sv + kDataNum)) : -1;
    return s >= 0 && s <= 2 ? s : -1;
}

// the name as the game keeps it: getCardName's object (+0: the UTF-16 text)
std::u16string read_name() {
    const uint32_t sv = save_info();
    const int s = slot(sv);
    if (s < 0) return {};
    // getCardName's object, as 0x02720154 computes it: its own buffer (+0xC; its pointer may be on the whole
    // name, long_name.h)
    const uint32_t text = sv + kCardInfo + 0xA0 + uint32_t(s) * 0x40 + 0xC;
    std::u16string out;
    if (text < mem::kMem2Start || text >= mem::kMem2End - 2 * kMaxLen) return out;
    for (int i = 0; i < kGameLen; i++) {
        const uint16_t ch = ld16(text + 2 * uint32_t(i));
        if (!ch) break;
        out.push_back(char16_t(ch));
    }
    return out;
}

}  // namespace

// (the title and the file select already hold every file's name, and mDataNum is 0 there)
bool loaded() { return ss::in_gameplay() && slot(save_info()) >= 0; }

std::string current() {
    const std::u16string whole = long_name::shown(slot(save_info()));
    return story_lyt_text::to_utf8(whole.empty() ? read_name() : whole);
}

void ask() {
    if (!loaded()) return;
    text_entry::Request r;
    r.initial = long_name::shown(slot(save_info()));
    if (r.initial.empty()) r.initial = read_name();
    r.max_len = kMaxLen;
    r.language = int(console_language());  // the keyboard layout (AZERTY, QWERTZ, ...) and the name font
    // setCardName formats the name (0x02759D3C: a printf-style format string), here and at every start
    // when the names come from cking.sav: a "%" could read or write anywhere
    r.refused = kRefused;
    auto done = [](bool ok, std::u16string text) {
        if (!ok) return;
        if (text.find_first_not_of(u' ') == std::u16string::npos) {  // an empty name keeps the old one
            LOG("[player-name] no name given: the name stays");
            return;
        }
        if (text.find_first_of(kRefused) != std::u16string::npos) {  // (the host's own prompt takes any character)
            LOG("[player-name] a name with %% can't be used: the name stays");
            return;
        }
        std::lock_guard lock(g_mu);
        g_name = std::move(text);
        g_pending = true;
    };
    // the overlay's prompt; the host's own (window title, macOS sheet) where the overlay can't show
    if (!text_entry::start(r, done)) input::prompt_text(r.initial, kMaxLen, done);
}

// test aid: WWHD_TEST_PLAYER_NAME=name (UTF-8) renames the file being played once, as the settings page's prompt
// does, as soon as a file is played, or from TV frame WWHD_TEST_PLAYER_NAME_FRAME on (after a save state's load)
// (scripted runs: the whole name of long_name.h)
void test_rename() {
    static bool done = false;
    const char* e = getenv("WWHD_TEST_PLAYER_NAME");
    if (done || !e || !*e || !loaded()) return;
    static const uint64_t from = [] { const char* f = getenv("WWHD_TEST_PLAYER_NAME_FRAME"); return f ? strtoull(f, nullptr, 10) : 0ull; }();
    if (render::frame_count() < from) return;
    done = true;
    std::u16string name;
    for (const char* p = e; *p;) {  // (UTF-8 to UTF-16, up to three bytes)
        const unsigned char b = (unsigned char)*p;
        const int n = b < 0x80 ? 1 : b < 0xE0 ? 2 : 3;
        char16_t ch = n == 1 ? b : n == 2 ? (b & 0x1F) : (b & 0x0F);
        for (int k = 1; k < n && p[k]; k++) ch = char16_t(ch << 6 | (p[k] & 0x3F));
        name.push_back(ch);
        p += n;
    }
    std::lock_guard lock(g_mu);
    g_name = name.substr(0, kMaxLen);
    g_pending = true;
    LOG("[player-name] test: renaming the file being played");
}

void service(Cpu* c) {
    test_rename();
    std::u16string name;
    {
        std::lock_guard lock(g_mu);
        if (!g_pending) return;
        g_pending = false;
        name = std::move(g_name);
    }
    const uint32_t sv = save_info();
    const int s = slot(sv);
    if (s < 0 || !ss::in_gameplay()) {
        LOG("[player-name] no save file is being played: the new name is dropped");
        return;
    }
    // the game's own setter (the one that reads the names from cking.sav): 8 characters from an
    // 18-byte UTF-16 buffer
    static const uint32_t buf = mem::host_alloc(0x20, 16);
    std::memset(mem::ptr(buf), 0, 0x20);
    for (size_t i = 0; i < name.size() && i < size_t(kGameLen); i++) st16(buf + 2 * uint32_t(i), uint16_t(name[i]));
    long_name::release(s);  // the setter writes through the object's pointer: onto the file's own buffer
    Cpu call = *c;
    const uint32_t object = guest_call(&call, fn_get_card_name, {sv + kCardInfo, uint32_t(s)});
    guest_call(&call, fn_set_card_name, {object, buf});
    long_name::renamed(s, name);  // over 8 characters: the whole name, kept with the next save
    LOG("[player-name] save file %d: Link is now \"%s\"", s + 1, current().c_str());
}

}  // namespace player_name
