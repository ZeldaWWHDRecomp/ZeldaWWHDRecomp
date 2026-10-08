// Language sources (game_languages.h, docs/language-packs.md): tells the USA game code the region and
// language of the European or Japanese pack the player chose.
//
// 0x025F9448 reads the console language into the system setting object (r3, the singleton at
// 0x101F4BAC): +0x10 region (the USA build always stores 2, or 1 when the setting can't be read),
// +0x14 language (only 1, 2 and 5 are kept, others become 1), and mirrors the language into the
// save's options (save + 0x12F0, byte +4: 0 English, 2 French, 3 Spanish). The region and language
// decide the 2D pack (0x02612BE0: region 4 -> Eu<Language> for languages 1..5, region 1 ->
// JpJapanese), the language suffix of layout panes (0x0270517C / 0x027051B0: _EuGe, _JpJa, ...), the
// software keyboard (0x026195C4: Japanese kana keyboard for region 1, QWERTZ/AZERTY... for region 4),
// the error viewer's region and language, and the time format of messages. All of these have their
// European and Japanese branches in the USA code; only this reader is limited to the USA.
//
// Tested with the European game; the Japanese region (1) is untested.
#include <cstring>
#include <string>

#include "game_languages.h"
#include "guest_addr.h"
#include "runtime.h"

extern "C" {
// Only the USA build is hooked here (tools/recomp/hooks_language.txt says "# builds: USA"): the
// European and Japanese builds read their own region. The runtime ships for every build, so the
// A weak no-op keeps the unused wrapper linkable on every platform. The generated USA
// original is strong and overrides it; the EUR build never calls this wrapper.
__attribute__((weak)) void f_025F9448_orig(Cpu*) {}  // SysSetting::update
}

extern "C" void hook_025F9448(Cpu* c) {
    const uint32_t self = c->r[3];
    f_025F9448_orig(c);
    const game_lang::Start s = game_lang::current();
    if (!s.pack || !self) return;
    st32(self + 0x10, (uint32_t)s.region);
    st32(self + 0x14, (uint32_t)s.language);
    // the save's options, as the European game would keep them (only copied to 0x101EA6D2 by the
    // name scene in the USA code, never read there)
    const uint32_t save = ld32(GD(0x101F84DC));
    if (save) st8(save + 0x12F0 + 4, (uint8_t)game_lang::options_language(s.language));
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOG("[config] language source active: %s (%s), %s; the game is told region %d, language %d",
            game_lang::name(s.language), game_lang::region_name(s.region), s.pack->file.c_str(), s.region, s.language);
    }
}

// German genitive of the player's name (the European game's 0x025F85AC; the USA build has no such code).
// The German text of three messages puts the name tag where German needs the genitive ("Links
// Schwester", "Lukas' Schwester"): the European code appends "s" to the name, or "'" when it ends in
// s, x or z (either case), for messages 0xC8B, 0x1D21 and 0x31D7 when the console language is German.
// The USA function (0x025F8618: the name into a 20-byte buffer for the message tag) gets the message
// id in r5 from its only caller (0x025F8854, as in the European build) but ignores it. The same bytes
// are appended here after the USA code has copied and terminated the name (0x025F871C), with the
// European bound (at most capacity - 1 bytes in total).
namespace {
thread_local uint32_t t_name_message = 0;

bool german_europe() {
    const uint32_t setting = ld32(GD(0x101F4BAC));
    return setting && ld32(setting + 0x10) == (uint32_t)game_lang::kEurope && ld32(setting + 0x14) == 3;
}
}  // namespace

extern "C" void site_025F8618(Cpu* c) { t_name_message = c->r[5]; }

extern "C" void site_025F8720(Cpu* c) {
    const uint32_t id = t_name_message;
    t_name_message = 0;
    if (id != 0xC8B && id != 0x1D21 && id != 0x31D7) return;
    if (!german_europe()) return;
    const uint32_t buf = c->r[30];
    const int32_t cap = (int32_t)ld32(c->r[1] + 0x18);
    int32_t len = 0;
    while (len < cap && ld8(buf + len)) len++;
    std::string name;
    for (int32_t i = 0; i < len; i++) name.push_back((char)ld8(buf + i));
    const char* suffix = game_lang::german_genitive_suffix(name);
    int32_t n = (int32_t)strlen(suffix);
    if (n > cap - len - 1) n = cap - len - 1;
    if (n <= 0) return;
    for (int32_t i = 0; i < n; i++) st8(buf + len + i, (uint8_t)suffix[i]);
    st8(buf + len + n, 0);
}

// The same in the message text itself: putPlayerName (0x025FC444; the European 0x025FC700 reads the
// message id at *(self)+0x11C and appends the wide "s" / "'" to the name before it is put). Here after
// the USA code has the name (or the default name) in its 32-character buffer (sp+0x10, capacity at
// sp+0x18), before it measures and puts it (0x025FC668).
extern "C" void site_025FC668(Cpu* c) {
    const uint32_t self = c->r[25];
    const uint32_t obj = self ? ld32(self) : 0;
    const uint32_t id = obj ? ld32(obj + 0x11C) : 0;
    if (id != 0xC8B && id != 0x1D21 && id != 0x31D7) return;
    if (!german_europe()) return;
    const uint32_t buf = ld32(c->r[1] + 0x10);
    const int32_t cap = (int32_t)ld32(c->r[1] + 0x18);
    if (!buf || cap <= 0) return;
    int32_t len = 0;
    while (len < cap && ld16(buf + len * 2)) len++;
    std::string last;  // the last character, when it is ASCII (the rule only looks at s, x, z)
    if (len > 0 && ld16(buf + (len - 1) * 2) < 0x80) last.push_back((char)ld16(buf + (len - 1) * 2));
    else if (len > 0) last.push_back('?');
    const char* suffix = game_lang::german_genitive_suffix(last);
    int32_t n = (int32_t)strlen(suffix);
    if (n > cap - len - 1) n = cap - len - 1;
    if (n <= 0) return;
    for (int32_t i = 0; i < n; i++) st16(buf + (len + i) * 2, (uint16_t)(uint8_t)suffix[i]);
    st16(buf + (len + n) * 2, 0);
}
