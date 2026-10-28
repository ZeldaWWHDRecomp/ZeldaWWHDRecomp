// The context buttons' game-state reader (mods/play_context.h) on fixture memory: a sparse big-endian
// guest image laid out as the game has it (USA addresses; EUR differs only by GD()). No game files.
#include "mods/play_context.h"

#include <cassert>
#include <cstring>
#include <map>

using namespace mods::playctx;

namespace {
struct Fixture {
    std::map<uint32_t, uint8_t> bytes;
    uint8_t u8(uint32_t a) const {
        auto it = bytes.find(a);
        return it == bytes.end() ? 0 : it->second;
    }
    uint16_t u16(uint32_t a) const { return uint16_t(u8(a) << 8 | u8(a + 1)); }
    uint32_t u32(uint32_t a) const { return uint32_t(u16(a)) << 16 | u16(a + 2); }
    bool object(uint32_t p, uint32_t size) const { return (p & 3) == 0 && p >= 0x10000000 && p <= 0x50000000 - size; }
    void s8(uint32_t a, uint8_t v) { bytes[a] = v; }
    void s16(uint32_t a, uint16_t v) { s8(a, uint8_t(v >> 8)); s8(a + 1, uint8_t(v)); }
    void s32(uint32_t a, uint32_t v) { s16(a, uint16_t(v >> 16)); s16(a + 2, uint16_t(v)); }
    void str(uint32_t a, const char* s) { for (int i = 0; i < 8; i++) s8(a + i, i < (int)strlen(s) ? uint8_t(s[i]) : 0); }
};

// the USA addresses of g_dComIfG_gameInfo, l_fopOvlpM_overlap[0] and the save info pointer
constexpr uint32_t G = 0x1046F0B0, kOverlap = 0x101F36CC, kSaveInfoPtr = 0x101F84DC;
const Addresses kUsa{G, kOverlap, kSaveInfoPtr};
constexpr uint32_t kLinkAddr = 0x16000000, kAtt = 0x16100000, kEntry = 0x16100054, kEnemy = 0x17000000,
                   kNpc = 0x17100000, kSave = 0x18000000;

// Outset, on foot, with a save loaded and Link controlled
Fixture outset() {
    Fixture f;
    f.str(G + kStage, "sea");
    f.s32(kSaveInfoPtr, kSave);
    f.s16(kSave + 0x20 + kSaveMaxLife, 12);
    f.s16(kSave + 0x20 + kSaveLife, 12);
    for (int i = 0; i < 21; i++) f.s8(kSave + 0x20 + kSaveInventory + i, kItemNone);
    f.s32(G + kPlayer, kLinkAddr);
    f.s32(kLinkAddr + kLinkCurProc, 0x04);  // standing
    f.s16(kLinkAddr + kLinkEquipItem, kHandNone);
    f.s8(kLinkAddr + kLinkReadyItemBtn, 0xFF);
    f.s32(kLinkAddr + kLinkAttention, kAtt);
    f.s32(kAtt + kAttOwner, kLinkAddr);
    f.s8(G + kSelectItem, kItemNone);
    f.s8(G + kSelectItem + 1, kItemNone);
    f.s8(G + kSelectItem + 2, kItemNone);
    return f;
}

PlayContext read_at(const Fixture& f, LockTracker& lock, GameUiSignals ui = {}, PaneSignals panes = {}, uint64_t step = 1) {
    return read(f, kUsa, ui, panes, lock, step);
}
}  // namespace

static void title_screen() {
    Fixture f;
    f.str(G + kStage, "sea_T");
    f.s32(G + kPlayer, kLinkAddr);
    f.s8(G + kDoStatus, kActSpeak);
    LockTracker lock;
    PlayContext c = read_at(f, lock);
    assert(!c.has(kInGame) && !c.has(kLink) && !c.has(kInPlay) && c.a_action == 0);
    f.str(G + kStage, "Name");  // the file select
    assert(!read_at(f, lock).has(kInGame));
    f.str(G + kStage, "");  // nothing loaded yet
    assert(!read_at(f, lock).has(kInGame));
    f.s32(kOverlap, 0x10200000);  // a fade shows also before a Quest Log is played
    assert(read_at(f, lock).has(kFade));
}

static void actions_and_items() {
    Fixture f = outset();
    f.s8(G + kDoStatus, kActLift);
    f.s8(G + kAStatus, kActSword);
    f.s8(G + kRStatus, kActCrouch);
    f.s8(G + kSelectItem, 0x27);      // X: bow
    f.s8(G + kSelectItem + 1, 0x23);  // Y: Picto Box
    f.s8(G + kSelectItem + 2, 0x20);  // R: telescope
    f.s8(kSave + 0x20 + kSaveInventory + 2, kItemWindWaker);
    f.s8(kSave + 0x20 + kSaveInventory + 1, kItemSwiftSail);
    f.s32(kLinkAddr + kLinkModeFlg, kModeCrouch);
    f.s32(kLinkAddr + kLinkAttnEntryA, kEntry);
    f.s32(kEntry + kAttListType, kAttnCarry);
    f.s32(G + kPlayerStatus0, kSt0Swim);
    f.s32(G + kPlayerStatus1, kSt1PictoAim);
    f.s8(G + kMiniGameType, 3);
    LockTracker lock;
    PlayContext c = read_at(f, lock, {}, {}, 77);
    assert(c.step == 77);
    assert(c.has(kInGame) && c.has(kLink) && c.has(kInPlay) && c.has(kHasWindWaker) && c.has(kHasSwiftSail));
    assert(c.a_action == kActLift && c.b_action == kActSword && c.zr_action == kActCrouch);
    assert(c.a_normal == kActLift && c.a_forced == 0);
    assert(c.x_item == 0x27 && c.y_item == 0x23 && c.r_item == 0x20);
    assert(c.attention_type == kAttnCarry && c.proc == 0x04 && c.item_in_hand == kHandNone);
    assert(c.mode_flags == kModeCrouch && c.player_status0 == kSt0Swim && c.player_status1 == kSt1PictoAim);
    assert(c.minigame == 3 && c.ready_item_button == 0xFF);
    // a forced action wins over the normal one (the Picto Box's ZR: album / aim)
    f.s8(G + kRStatus + kForce, kActPictoAim);
    f.s8(G + kDoStatus + kForce, kActTakePhoto);
    c = read_at(f, lock);
    assert(c.zr_action == kActPictoAim && c.zr_normal == kActCrouch && c.zr_forced == kActPictoAim);
    assert(c.a_action == kActTakePhoto && c.a_normal == kActLift);
    // an attention entry type past the known ones, or a bad pointer: none
    f.s32(kEntry + kAttListType, 42);
    assert(read_at(f, lock).attention_type == kAttnNone);
    f.s32(kLinkAddr + kLinkAttnEntryA, 0x00001234);
    assert(read_at(f, lock).attention_type == kAttnNone);
    // a save that doesn't look loaded (life over its maximum): no inventory reads
    f.s16(kSave + 0x20 + kSaveLife, 13);
    assert(!read_at(f, lock).has(kHasWindWaker));
}

static void play_gates() {
    Fixture f = outset();
    LockTracker lock;
    assert(read_at(f, lock).has(kInPlay));
    // each of the game's own screens takes Link out of the player's hands
    for (GameUiSignals ui : {GameUiSignals{true, false, false}, GameUiSignals{false, true, false}, GameUiSignals{false, false, true}}) {
        f.s8(G + kMesgStatus, ui.message ? 1 : 0);  // a talk's own message status
        const PlayContext c = read_at(f, lock, ui);
        assert(c.has(kInGame) && c.has(kLink) && !c.has(kInPlay));
        assert(c.has(kEvent) == ui.event && c.has(kMessage) == ui.message && c.has(kMenu) == ui.menu);
    }
    f.s8(G + kMesgStatus, 0);
    f.s32(kOverlap, 0x10200000);
    assert(!read_at(f, lock).has(kInPlay) && read_at(f, lock).has(kFade));
    f.s32(kOverlap, 0);
    f.s8(G + kNextStageEnable, 1);
    assert(!read_at(f, lock).has(kInPlay) && read_at(f, lock).has(kStageChange));
    f.s8(G + kNextStageEnable, 0);
    f.s8(G + kMesgStatus, 6);
    f.s8(G + kScopeMesgStatus, 2);
    PlayContext c = read_at(f, lock);
    assert(c.mesg_status == 6 && c.scope_mesg_status == 2);
    // another actor controlled, or an unaligned / out-of-heap pointer: no Link
    f.s32(G + kPlayer, 0);
    c = read_at(f, lock);
    assert(c.has(kInGame) && !c.has(kLink) && !c.has(kInPlay) && c.proc == -1 && c.item_in_hand == kHandUnknown);
    f.s32(G + kPlayer, kLinkAddr + 2);
    assert(!read_at(f, lock).has(kLink));
    f.s32(G + kPlayer, 0x60000000);
    assert(!read_at(f, lock).has(kLink));
    // the pane observer's signals pass through as they are
    f.s32(G + kPlayer, kLinkAddr);
    PaneSignals p;
    p.flags = kKeyIcon | kChoice | kMsgWindow;
    p.choice_count = 3;
    p.cursor_y = -120;
    p.msg_hw = 450;
    c = read_at(f, lock, {false, true, false}, p);
    assert(c.has(kKeyIcon) && c.has(kChoice) && c.has(kMsgWindow) && c.choice_count == 3 && c.cursor_y == -120 && c.msg_hw == 450);
}

static void shops_and_scopes() {
    Fixture f = outset();
    LockTracker lock;
    // Beedle's item choice (live trace): a talk prompting A Choose / B Cancel, no choice box
    f.s8(G + kMesgStatus, 14);
    f.s8(G + kDoStatus, kActChoose);
    f.s8(G + kAStatus, kActCancel);
    PlayContext c = read_at(f, lock, {true, true, false});
    assert(c.has(kShop) && !c.has(kInPlay));
    // a yes / no choice box is a choice, not a shop
    PaneSignals p;
    p.flags = kChoice;
    assert(!read_at(f, lock, {true, true, false}, p).has(kShop));
    // no talk: no shop
    assert(!read_at(f, lock, {false, false, false}).has(kShop));
    // the Picto Box (live: scope message status 11, hud_gate says message): still in play
    f.s8(G + kMesgStatus, 0);
    f.s8(G + kScopeMesgStatus, 11);
    f.s8(G + kDoStatus, kActTakePhoto);
    c = read_at(f, lock, {false, true, false});
    assert(c.has(kMessage) && c.has(kInPlay) && !c.has(kShop) && !talking(c));
}

static void lock_on_and_defeats() {
    Fixture f = outset();
    LockTracker lock;
    // an enemy in reach, none locked
    f.s32(kAtt + kAttLockCount, 2);
    PlayContext c = read_at(f, lock);
    assert(c.lock_state == 0 && c.lock_reach == 2 && !c.has(kLocked) && c.defeats == 0);
    // locked on an enemy with health
    f.s32(kEnemy + kActorBsType, 2);
    f.s32(kEnemy + kActorProcId, 0x1234);
    f.s32(kEnemy + kActorAttnFlags, kAttnLockonBattle);
    f.s8(kEnemy + kActorHealth, 3);
    f.s8(kAtt + kAttLockState, 1);
    f.s32(kLinkAddr + kLinkLockOnActor, kEnemy);
    c = read_at(f, lock);
    assert(c.has(kLocked) && c.lock_state == 1 && c.lock_reach == 0);
    // its health reaches 0 while still locked, then the lock ends: one defeat
    f.s8(kEnemy + kActorHealth, 0);
    assert(read_at(f, lock).defeats == 0);
    f.s8(kAtt + kAttLockState, 0);
    f.s32(kLinkAddr + kLinkLockOnActor, 0);
    c = read_at(f, lock);
    assert(!c.has(kLocked) && c.defeats == 1);
    assert(read_at(f, lock).defeats == 1);  // counted once
    // a second enemy: the lock lets go by distance while it lives; watched a second, then forgotten
    f.s8(kEnemy + kActorHealth, 4);
    f.s32(kEnemy + kActorProcId, 0x1235);
    f.s8(kAtt + kAttLockState, 1);
    f.s32(kLinkAddr + kLinkLockOnActor, kEnemy);
    read_at(f, lock);
    f.s8(kAtt + kAttLockState, 2);  // release, no target any more
    f.s32(kLinkAddr + kLinkLockOnActor, 0);
    for (int i = 0; i < LockTracker::kWatchSteps + 5; i++) assert(read_at(f, lock).defeats == 1);
    // later it dies on its own: no defeat (the watch ended)
    f.s8(kEnemy + kActorHealth, 0);
    assert(read_at(f, lock).defeats == 1);
    // a third: deleted a few steps after the lock ended (fpcBs_Delete clears mBsType): a defeat
    f.s8(kEnemy + kActorHealth, 2);
    f.s32(kEnemy + kActorProcId, 0x1236);
    f.s8(kAtt + kAttLockState, 1);
    f.s32(kLinkAddr + kLinkLockOnActor, kEnemy);
    read_at(f, lock);
    f.s8(kAtt + kAttLockState, 0);
    f.s32(kLinkAddr + kLinkLockOnActor, 0);
    assert(read_at(f, lock).defeats == 1);
    assert(read_at(f, lock).defeats == 1);
    f.s32(kEnemy + kActorBsType, 0);
    assert(read_at(f, lock).defeats == 2);
    // locking a person (not an enemy) is never a defeat
    f.s32(kNpc + kActorBsType, 2);
    f.s32(kNpc + kActorProcId, 0x2000);
    f.s32(kNpc + kActorAttnFlags, 0x2);  // fopAc_Attn_LOCKON_TALK_e
    f.s8(kAtt + kAttLockState, 1);
    f.s32(kLinkAddr + kLinkLockOnActor, kNpc);
    read_at(f, lock);
    f.s8(kAtt + kAttLockState, 0);
    f.s32(kLinkAddr + kLinkLockOnActor, 0);
    f.s32(kNpc + kActorBsType, 0);
    for (int i = 0; i < 40; i++) assert(read_at(f, lock).defeats == 2);
    // an attention object that isn't Link's (mpPlayer differs): no lock reading at all
    f.s32(kAtt + kAttOwner, kNpc);
    f.s8(kAtt + kAttLockState, 1);
    c = read_at(f, lock);
    assert(c.lock_state == 0 && !c.has(kLocked) && c.defeats == 2);
    // the count stays on the title screen
    f.str(G + kStage, "sea_T");
    assert(read_at(f, lock).defeats == 2);
}

static void panes() {
    assert(classify_pane("L_KeyIcon_00") == kPaneKeyIcon);
    assert(classify_pane("W_Choise2_00") == kPaneChoice2 && classify_pane("W_Choise3_00") == kPaneChoice3);
    assert(classify_pane("W_Cursor_00") == kPaneCursor && classify_pane("P_SelNumber_00") == kPaneNumber);
    assert(classify_pane("P_MsgWindow_00") == kPaneMsgWindow && classify_pane("N_Finder_00") == kPaneFinder);
    assert(classify_pane("N_PictographAll_00") == kPaneAlbum && classify_pane("P_WindDirectionBase_00") == kPaneWind);
    assert(classify_pane("N_Zoom_00") == kPaneZoom);
    assert(classify_pane("L_KeyIcon_01") == kPaneNone && classify_pane("N_Zoom_000") == kPaneNone);
    assert(classify_pane("") == kPaneNone && classify_pane("T_X_00") == kPaneNone);
    assert(zoom_kind("P_Bar_00") == 1 && zoom_kind("P_TelescopeBar_00") == 2 && zoom_kind("P_Other_00") == 0);
    uint64_t seen[kPaneSignalCount] = {};
    assert(aged_pane_flags(seen, 100) == 0);
    seen[kPaneKeyIcon] = 100 + 1;  // drawn on step 100
    assert(aged_pane_flags(seen, 100) == kKeyIcon && aged_pane_flags(seen, 103) == kKeyIcon);
    assert(aged_pane_flags(seen, 104) == 0);  // older than kPaneSteps
    // a choice needs its cursor
    seen[kPaneChoice2] = 101;
    assert(!(aged_pane_flags(seen, 100) & kChoice));
    seen[kPaneCursor] = 101;
    assert(aged_pane_flags(seen, 100) & kChoice);
    seen[kPaneZoom] = seen[kPaneFinder] = seen[kPaneAlbum] = seen[kPaneWind] = seen[kPaneNumber] = seen[kPaneMsgWindow] = 101;
    const uint32_t all = aged_pane_flags(seen, 100);
    assert((all & (kZoomBar | kPictoFinder | kAlbum | kWindCompass | kNumberEntry | kMsgWindow)) ==
           (kZoomBar | kPictoFinder | kAlbum | kWindCompass | kNumberEntry | kMsgWindow));
}

static void pack_and_trace() {
    Fixture f = outset();
    f.s8(G + kDoStatus, kActSpeak);
    f.s8(G + kSelectItem + 2, 0x27);
    LockTracker lock;
    const PlayContext c = read_at(f, lock, {}, {}, 5);
    int32_t v[kPackLen + 4];
    for (int32_t& x : v) x = -7;
    pack(c, v, kPackLen + 4);
    assert(v[0] == kPackVersion && uint32_t(v[1]) == c.flags && v[2] == kActSpeak && v[7] == 0x27);
    assert(v[8] == kHandNone && v[9] == 4 && v[31] == 5 && v[kPackLen] == 0 && v[kPackLen + 3] == 0);
    int32_t few[3];
    pack(c, few, 3);  // a shorter array takes the front
    assert(few[0] == kPackVersion && few[2] == kActSpeak);
    char buf[512];
    format(c, buf, sizeof buf);
    assert(strstr(buf, "flags=game,link,play") && strstr(buf, "A=02") && strstr(buf, "R=27"));
    PlayContext d = c;
    d.step = 6;
    d.cursor_y = 33;
    assert(same_for_log(c, d));  // the step and drawn positions alone are no change
    d.a_action = kActCheck;
    assert(!same_for_log(c, d));
}

int main() {
    title_screen();
    actions_and_items();
    play_gates();
    shops_and_scopes();
    lock_on_and_defeats();
    panes();
    pack_and_trace();
    return 0;
}
