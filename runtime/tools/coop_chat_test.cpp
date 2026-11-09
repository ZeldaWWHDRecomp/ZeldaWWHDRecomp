// SPDX-License-Identifier: MPL-2.0
// Online co-op's text chat, the pure part of runtime/src/mods/coop_chat.h: the tabs (Party first, Global only where
// the server allows it), unread counts, a channel turned off, local mutes and their file, fading and the history.
#include <cstdio>
#include <string>

#include "mods/coop_chat.h"

using namespace mods::coop::chat;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)

static Line line(int channel, uint64_t key, const char* name, const char* text, bool yours = false) {
    Line l;
    l.channel = channel, l.key = key, l.name = name, l.text = text, l.yours = yours;
    return l;
}

int main() {
    Box b;
    // no rules (a server that never sent them, or the direct transport): no chat at all
    CHECK(!b.shown() && b.tabs().empty() && b.active() == -1);
    CHECK(!b.receive(line(kParty, 1, "A", "hi"), 100));
    // chat on, global off: only the Party tab
    b.set_rules({true, false, 120});
    CHECK(b.shown() && b.tabs() == std::vector<int>{kParty} && b.active() == kParty);
    CHECK(!b.receive(line(kGlobal, 1, "A", "hi"), 100));  // (no global channel here)
    // global on: two tabs, Party stays active
    b.set_rules({true, true, 120});
    CHECK((b.tabs() == std::vector<int>{kParty, kGlobal}) && b.active() == kParty);
    CHECK(b.receive(line(kParty, 1, "Tetra", "Follow me"), 1000));
    CHECK(b.receive(line(kGlobal, 2, "Sailor 7Q2F", "hello"), 1100));
    CHECK(b.receive(line(kGlobal, 3, "Sailor 9ABC", "hey"), 1200));
    CHECK(b.unread(kParty) == 0 && b.unread(kGlobal) == 2);  // the inactive tab counts, the box shows the active one's
    auto f = b.fading(1300);
    CHECK(f.size() == 1 && f[0].first->text == "Follow me" && f[0].second == 1.0f);
    // fading: full for kShowMs, then down to nothing over kFadeMs
    f = b.fading(1000 + kShowMs + kFadeMs / 2);
    CHECK(f.size() == 1 && f[0].second > 0.4f && f[0].second < 0.6f);
    CHECK(b.fading(1000 + kShowMs + kFadeMs).empty());
    // switching: the unread count goes, the box shows that tab's lines
    b.next_tab();
    CHECK(b.active() == kGlobal && b.unread(kGlobal) == 0);
    f = b.fading(1300);
    CHECK(f.size() == 2 && f[1].first->text == "hey");
    CHECK(b.receive(line(kParty, 1, "Tetra", "Wait"), 1400));
    CHECK(b.unread(kParty) == 1);
    b.select(kParty);
    CHECK(b.active() == kParty && b.unread(kParty) == 0);
    b.next_tab(), b.next_tab();  // (wraps around)
    CHECK(b.active() == kParty);
    // one's own lines never count as unread
    b.select(kParty);
    CHECK(b.receive(line(kGlobal, 9, "Me", "mine", true), 1500) && b.unread(kGlobal) == 0);
    // Global turned off: its tab and its lines go, nothing more is kept there; back on: empty
    b.set_prefs({true, false});
    CHECK(b.tabs() == std::vector<int>{kParty} && b.history(kGlobal).empty() && !b.receive(line(kGlobal, 2, "S", "x"), 1600));
    b.set_prefs({true, true});
    CHECK(b.history(kGlobal).empty() && b.tabs().size() == 2);
    // Party off with Global on: Global becomes the active tab
    b.set_prefs({false, true});
    CHECK(b.active() == kGlobal && b.tabs() == std::vector<int>{kGlobal} && !b.receive(line(kParty, 1, "T", "x"), 1700));
    b.set_prefs({false, false});
    CHECK(!b.shown() && b.active() == -1);
    b.set_prefs({true, true});
    CHECK(b.active() == kParty);
    // the server turns chat off: nothing shows
    b.set_rules({false, false, 120});
    CHECK(!b.shown());
    b.set_rules({true, true, 120});
    // mutes: their lines go (from the history too) and stay away; the list's file form
    CHECK(b.receive(line(kParty, 5, "Noisy", "a"), 2000) && b.receive(line(kParty, 6, "Kind", "b"), 2001));
    CHECK(b.mute(5, "Noisy", true) && b.muted(5) && !b.mute(0, "nobody", true));
    CHECK(b.history(kParty).back().text == "b");
    for (const Line& l : b.history(kParty)) CHECK(l.key != 5);
    CHECK(!b.receive(line(kParty, 5, "Noisy", "again"), 2002) && b.receive(line(kParty, 5, "Me?", "own", true), 2003));
    const std::string file = b.mutes_text();
    CHECK(file == "mute 0000000000000005 Noisy\n");
    Box c;
    c.parse_mutes(file + "garbage\nmute zz00000000000005 x\n");
    CHECK(c.mutes().size() == 1 && c.muted(5) && c.mutes()[0].name == "Noisy");
    CHECK(b.mute(5, "", false) && !b.muted(5) && !b.mute(5, "", false));
    // the senders to mute from (newest first, once each, not one's own)
    auto s = b.senders();
    CHECK(!s.empty() && s[0].key == 6);
    // a note (a line not passed on) shows in its channel
    b.note(kGlobal, "Not sent: too many messages", 3000);
    CHECK(b.history(kGlobal).back().text == "Not sent: too many messages" && b.history(kGlobal).back().key == 0);
    // the history keeps the newest kHistory lines; the box at most kBoxLines
    for (int i = 0; i < 70; i++) b.receive(line(kParty, 6, "Kind", std::to_string(i).c_str()), 4000 + i);
    CHECK(b.history(kParty).size() == kHistory && b.history(kParty).back().text == "69" && b.history(kParty).front().text == "20");
    CHECK(b.fading(4100).size() == kBoxLines);
    CHECK(b.recent(4100, 1000) && !b.recent(9000, 1000));
    b.clear_lines();
    CHECK(b.history(kParty).empty() && b.mutes().size() == 0);
    if (g_fail) {
        std::printf("coop_chat_test: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("coop_chat_test: all passed\n");
    return 0;
}
