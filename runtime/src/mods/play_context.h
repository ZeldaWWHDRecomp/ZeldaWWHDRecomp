// SPDX-License-Identifier: MPL-2.0
// What the game offers the player right now (play_context.cpp reads it once per full logic step):
// the game's own button prompts, Link's mode, the item in his hand, the items on X / Y / R, the
// lock-on, what he faces, the message box and its parts, the Picto Box, shops, events, fades and
// menus. One POD struct, PlayContext, so the context buttons (context_buttons.h), the overlay, a
// host-drawn HUD and the Android touch controls all decide from the same reading.
//
// This header is pure: the reader is a template over a memory view, so play_context_test checks
// it on fixture memory without the game. The runtime side (guest memory, the Pane::Draw observer,
// publication, the WWHD_PLAYCTX_LOG trace) is play_context.cpp.
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// mods/touch_state.cpp and hud.cpp; reimplemented for our design, every offset re-checked against
// runtime/guest/include/wwhd/link.h, the decompilation and live memory (USA and EUR).
//
// Offsets. g_dComIfG_gameInfo (dComIfGp_get() 025200D4) is at GD(0x1046F0B0); its play member
// (dComIfG_play_c) starts at +0x12A0, so a play member at decomp offset P is at gameInfo + 0x12A0 + P
// (the same layout in USA and EUR; only GD() moves the base):
//   +0x5134 current stage name (char[8])          +0x514C dStage_nextStage_c enable (stage change)
//   +0x5292 dEvt_control_c mode (event running)   +0x5B2C mPlayerInfo[0].mpPlayer (Link when he plays)
//   +0x5BB2 mMesgStatus, +0x5BB3 mScopeMesgStatus (hud_gate.cpp, savestate.cpp)
//   +0x5BB5 mRStatus (GameCube R = Wii U ZR), +0x5BB6 mAStatus (B), +0x5BB7 mDoStatus (A);
//   +0x5BB8..0x5BBA their forced variants (mRStatusForce, mAStatusForce, mDoStatusForce)
//   +0x5BBB mSelectItem[5]: the item numbers on X, Y, R (quick_use.cpp, cheats.cpp)
//   +0x5CD8 / +0x5CDC mPlayerStatus[0][0..1] (daPyStts0_* / daPyStts1_*), +0x5CEA mMiniGameType
// daPy_lk_c (link.h): +0x65B0 mpAttnActorLockOn, +0x65F0 mCurProc, +0x6894 mpAttention (dAttention_c:
// +0x00 mpPlayer, +0x18 lock state, +0xD4 lock-on count), +0x6898 mpAttnEntryA (dAttList_c: +0x08 mType),
// +0x68D9 mReadyItemBtn, +0x69B0 mEquipItem, +0x6A70 mModeFlg.
// fopAc_ac_c: +0x00 mBsType (0 once deleted), +0x04 process id, +0x39C attention flags (4: an enemy's
// lock-on), +0x3A1 health (s8).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mods::playctx {

// ---- globals: g_dComIfG_gameInfo, l_fopOvlpM_overlap[0], the save info pointer; their addresses (USA
// 1046F0B0, 101F36CC, 101F84DC) are mapped through GD() in play_context.cpp (struct Addresses below) ----

// gameInfo-relative
constexpr uint32_t kStage = 0x5134, kNextStageEnable = 0x514C, kEventMode = 0x5292, kPlayer = 0x5B2C;
constexpr uint32_t kMesgStatus = 0x5BB2, kScopeMesgStatus = 0x5BB3;
constexpr uint32_t kRStatus = 0x5BB5, kAStatus = 0x5BB6, kDoStatus = 0x5BB7, kForce = 3;
constexpr uint32_t kSelectItem = 0x5BBB;
constexpr uint32_t kPlayerStatus0 = 0x5CD8, kPlayerStatus1 = 0x5CDC, kMiniGameType = 0x5CEA;
// daPy_lk_c
constexpr uint32_t kLinkLockOnActor = 0x65B0, kLinkCurProc = 0x65F0, kLinkAttention = 0x6894,
                   kLinkAttnEntryA = 0x6898, kLinkReadyItemBtn = 0x68D9, kLinkEquipItem = 0x69B0,
                   kLinkModeFlg = 0x6A70, kLinkSize = 0x8284;
// dAttention_c, dAttList_c
constexpr uint32_t kAttOwner = 0x00, kAttLockState = 0x18, kAttLockCount = 0xD4, kAttListType = 0x08;
// fopAc_ac_c
constexpr uint32_t kActorBsType = 0x00, kActorProcId = 0x04, kActorAttnFlags = 0x39C, kActorHealth = 0x3A1;
constexpr uint32_t kAttnLockonBattle = 0x4;  // fopAc_Attn_LOCKON_BATTLE_e
// dSv_player_c (save + 0x20): +0x00 max life, +0x02 life, +0x3C inventory u8[21]
constexpr uint32_t kSaveMaxLife = 0x00, kSaveLife = 0x02, kSaveInventory = 0x3C;

// ---- the game's words: dActionStatus_e (GameCube decompilation names; HD adds codes past 0x3E) ----
enum Action : uint8_t {
    kActNone = 0x00, kActLook = 0x01, kActSpeak = 0x02, kActCharts = 0x03, kActLift = 0x04, kActClimb = 0x05,
    kActLetGo = 0x06, kActReturn = 0x07, kActPutAway = 0x08, kActDrop = 0x09, kActCheck = 0x0A, kActOpen = 0x0B,
    kActAttack = 0x0C, kActThrow = 0x0E, kActCrouch = 0x0F, kActSidle = 0x10, kActGrab = 0x11, kActJump = 0x12,
    kActStop = 0x13, kActLetGoRope = 0x16, kActChoose = 0x17, kActNext = 0x19, kActPickUp = 0x1B,
    kActGetIn = 0x1C, kActGetOut = 0x1D, kActTakePhoto = 0x20, kActInfo = 0x21, kActSwapModes = 0x22,
    kActFly = 0x23, kActCall = 0x24, kActBid = 0x25, kActRead = 0x26, kActCancel = 0x27, kActCruise = 0x2C,
    kActSwing = 0x2F, kActSeaChart = 0x30, kActSword = 0x35, kActDefend = 0x36, kActBokoStick = 0x37,
    kActHidden = 0x3E,  // the game hides the whole button
    // HD, seen live (fork logs, our traces): on ZR in the Picto Box; A in its album
    kActAlbumNothing = 0x44, kActPictoAlbum = 0x48, kActPictoAim = 0x49,
};

// item numbers (dItemNo)
constexpr uint8_t kItemNone = 0xFF, kItemTelescope = 0x20, kItemWindWaker = 0x22, kItemPictoBox = 0x23,
                  kItemGrapplingHook = 0x25, kItemDeluxePictoBox = 0x26, kItemBow = 0x27, kItemDekuLeaf = 0x34,
                  kItemSwiftSail = 0x77, kItemSail = 0x78;
// mEquipItem (u16): what is in Link's hand
constexpr uint16_t kHandNone = 0x100, kHandSword = 0x103, kHandUnknown = 0xFFFF;

// daPyStts0_* (mPlayerStatus[0][0]) and daPyStts1_* ([0][1])
enum : uint32_t {
    kSt0Hang = 0x100, kSt0BowAim = 0x1000, kSt0FirstPerson = 0x2000, kSt0HookshotAim = 0x4000,
    kSt0SwordSwing = 0x8000, kSt0ShipRide = 0x10000, kSt0RopeAim = 0x20000, kSt0BoomerangAim = 0x80000,
    kSt0Swim = 0x100000, kSt0Telescope = 0x200000, kSt0BoomerangWait = 0x400000, kSt0Rope = 0x800000,
    kSt0Hang2 = 1u << 25, kSt0Ladder = 0x4000000, kSt0Crawl = 0x8000000, kSt0SpinAttack = 0x40000000,
    kSt0Aims = kSt0BowAim | kSt0HookshotAim | kSt0RopeAim | kSt0BoomerangAim,
};
enum : uint32_t { kSt1Conduct = 0x1, kSt1PictoAim = 0x8, kSt1LeafFly = 0x20, kSt1LeafFan = 0x40, kSt1Sail = 0x400 };
// daPy_lk_c::ModeFlg_* (mModeFlg)
enum : uint32_t {
    kModeMidair = 0x2, kModeHang = 0x20, kModeHookshot = 0x200, kModeRope = 0x800, kModeInShip = 0x2000,
    kModeClimb = 0x10000, kModeSwim = 0x40000, kModeGrab = 0x100000, kModePushPull = 0x200000,
    kModeLadder = 0x400000, kModeCrouch = 0x800000, kModeCrawl = 0x1000000, kModeSubject = 0x20000000,
};
// dAttList_c::mType (fopAc_Attn_TYPE_*): what A acts on
enum AttnType : uint8_t {
    kAttnMisc = 0, kAttnTalk = 1, kAttnBattle = 2, kAttnSpeak = 3, kAttnCarry = 4, kAttnDoor = 5,
    kAttnTreasure = 6, kAttnShip = 7, kAttnNone = 0xFF,
};

// ---- PlayContext ----
enum Flag : uint32_t {
    kInGame = 1u << 0,       // a Quest Log is played (not the title screen or the file select)
    kLink = 1u << 1,         // Link is the controlled actor (play mpPlayer[0] is a valid daPy_lk_c)
    kInPlay = 1u << 2,       // Link is under the player's control: in game, no event / talk / menu / fade / stage change
    kEvent = 1u << 3,        // dComIfGp_event_runCheck: a talk, a cutscene, a get-item scene...
    kMessage = 1u << 4,      // dComIfGp_getMesgStatus or the telescope's: a message box is open
    kMenu = 1u << 5,         // a game menu (hud_gate.cpp's reading: dMenu_flag, the in-game UI's menu states)
    kFade = 1u << 6,         // a fade or wipe is running (l_fopOvlpM_overlap)
    kStageChange = 1u << 7,  // a stage change is requested (dStage_nextStage_c enable)
    kLocked = 1u << 8,       // the lock-on holds (dAttention_c lock state 1, or 2 with a target)
    kKeyIcon = 1u << 9,      // the message's "go on" icon is shown (L_KeyIcon_00): A / a tap continues
    kChoice = 1u << 10,      // the message offers a choice (W_Choise2_00 / W_Choise3_00 with W_Cursor_00)
    kNumberEntry = 1u << 11, // the message's number entry (P_SelNumber_00: the auction's bid)
    kMsgWindow = 1u << 12,   // the message window itself (P_MsgWindow_00)
    kPictoFinder = 1u << 13, // the Picto Box's finder screen (PictographBox_00 N_Finder_00)
    kAlbum = 1u << 14,       // the picto album (N_PictographAll_00): the Picto Box's or the game's chooser
    kWindCompass = 1u << 15, // the wind compass after the Wind's Requiem (P_WindDirectionBase_00)
    kZoomBar = 1u << 16,     // the Picto Box's or the telescope's zoom bar (N_Zoom_00); zoom_kind says which
    kShop = 1u << 17,        // a shop's item choice: A Choose / B Cancel in a talk without a choice box
    kHasWindWaker = 1u << 18,  // the Wind Waker is in the inventory (d-pad up conducts)
    kHasSwiftSail = 1u << 19,  // the HD Swift Sail in place of the sail
};

struct PlayContext {
    uint64_t step = 0;     // logic step it was read on
    uint32_t flags = 0;    // Flag bits
    // the game's button prompts (Action codes; 0 none). The shown one is the forced one if set.
    uint8_t a_action = 0, b_action = 0, zr_action = 0;        // A (mDoStatus), B (mAStatus), ZR (mRStatus)
    uint8_t a_normal = 0, b_normal = 0, zr_normal = 0;
    uint8_t a_forced = 0, b_forced = 0, zr_forced = 0;
    uint8_t x_item = kItemNone, y_item = kItemNone, r_item = kItemNone;  // play mSelectItem
    uint8_t minigame = 0;            // mMiniGameType
    uint8_t ready_item_button = 0xFF;  // mReadyItemBtn: the button whose item is out (0 X, 1 Y, 2 R)
    uint8_t lock_state = 0;          // dAttention_c lock state: 0 none, 1 lock, 2 release
    uint8_t lock_reach = 0;          // targets in reach while none is locked (mLockonCount)
    uint8_t attention_type = kAttnNone;  // what A acts on (dAttList_c::mType of mpAttnEntryA)
    uint8_t mesg_status = 0, scope_mesg_status = 0;
    uint8_t choice_count = 0;        // 2 or 3 while kChoice
    uint8_t zoom_kind = 0;           // 1 Picto Box, 2 telescope (kZoomBar)
    uint16_t item_in_hand = kHandUnknown;  // mEquipItem (0x100 none, 0x103 sword)
    int32_t proc = -1;               // mCurProc (daPy_PROC id), -1 without Link
    uint32_t player_status0 = 0, player_status1 = 0, mode_flags = 0;
    uint32_t defeats = 0;            // locked enemies defeated so far (only grows)
    // message parts, layout units (centre origin, y up) as last drawn; 0 when not shown
    int16_t choice_y = 0, cursor_y = 0;            // the choice box's and the cursor's centre y
    int16_t msg_x = 0, msg_y = 0, msg_hw = 0, msg_hh = 0;  // the message window's centre and half size

    bool has(uint32_t f) const { return (flags & f) != 0; }
};

// ---- Link's mode, by the decompilation's names (pure helpers on the struct) ----
inline bool swimming(const PlayContext& c) { return (c.player_status0 & kSt0Swim) || (c.mode_flags & kModeSwim); }
inline bool in_ship(const PlayContext& c) { return (c.player_status0 & kSt0ShipRide) || (c.mode_flags & kModeInShip); }
// on a ladder, climbing ivy, hanging from a ledge (not the boat's own "hang": getting ready to sail)
inline bool climbing(const PlayContext& c) {
    return (c.mode_flags & (kModeLadder | kModeClimb | kModeHang)) && !(c.mode_flags & kModeInShip);
}
inline bool telescope(const PlayContext& c) { return (c.player_status0 & kSt0Telescope) != 0; }
inline bool aiming(const PlayContext& c) { return (c.player_status0 & kSt0Aims) != 0; }
inline bool first_person(const PlayContext& c) { return (c.player_status0 & kSt0FirstPerson) != 0; }
inline bool picto_mode(const PlayContext& c) {
    return (c.player_status1 & kSt1PictoAim) || c.zr_action == kActPictoAim || c.zr_action == kActPictoAlbum ||
           c.has(kPictoFinder) || c.has(kAlbum);
}
inline bool album(const PlayContext& c) { return c.zr_action == kActPictoAlbum || c.has(kAlbum); }
inline bool conducting(const PlayContext& c) { return (c.player_status1 & kSt1Conduct) != 0; }
inline bool leaf_flying(const PlayContext& c) { return (c.player_status1 & kSt1LeafFly) != 0; }
// Link carries something: A throws it (Throw) or ZR sets it down (Drop); not a Bokoblin's club
inline bool carrying(const PlayContext& c) {
    return c.b_action != kActBokoStick && (c.a_action == kActThrow || c.zr_action == kActDrop);
}
// an item is in his hand (not the sword, the Wind Waker or the Deku Leaf)
inline bool item_in_hand(const PlayContext& c) {
    const uint16_t h = c.item_in_hand;
    return h < 0xFF && h != kItemWindWaker && h != kItemDekuLeaf;
}
// a talk or a scene's text is up
// (the message status, not kMessage: that one also holds the telescope's / Picto Box's scope status)
inline bool talking(const PlayContext& c) { return c.mesg_status != 0 || c.has(kKeyIcon) || c.has(kMsgWindow); }

// ---- the layout panes the observer notes (nw::lyt::Pane names, as the game's layouts have them) ----
enum PaneSignal : int {
    kPaneKeyIcon,    // L_KeyIcon_00: the message's "go on" icon under the text
    kPaneChoice2,    // W_Choise2_00 / W_Choise3_00 in N_SubWindow_00: a choice of two or three lines
    kPaneChoice3,    //   (both stamp kPaneChoice2; this one only classifies)
    kPaneCursor,     // W_Cursor_00: the choice's cursor
    kPaneNumber,     // P_SelNumber_00: the message's number entry (the auction's bid)
    kPaneMsgWindow,  // P_MsgWindow_00 of MsgWindowMain_00: the message window's box
    kPaneFinder,     // N_Finder_00 of PictographBox_00: the Picto Box's finder
    kPaneAlbum,      // N_PictographAll_00: the picto album
    kPaneWind,       // P_WindDirectionBase_00 of ControlWind_00: the wind compass
    kPaneZoom,       // N_Zoom_00: the Picto Box's (first child P_Bar_00) or telescope's (P_TelescopeBar_00) zoom bar
    kPaneSignalCount,
    kPaneNone = kPaneSignalCount,
};
constexpr uint64_t kPaneSteps = 3;  // a part counts as shown while drawn within this many logic steps

inline PaneSignal classify_pane(const char* n) {
    auto is = [n](const char* s) { return strncmp(n, s, 24) == 0; };
    switch (n[0]) {
        case 'L': return is("L_KeyIcon_00") ? kPaneKeyIcon : kPaneNone;
        case 'W':
            if (is("W_Choise2_00")) return kPaneChoice2;
            if (is("W_Choise3_00")) return kPaneChoice3;
            return is("W_Cursor_00") ? kPaneCursor : kPaneNone;
        case 'P':
            if (is("P_SelNumber_00")) return kPaneNumber;
            if (is("P_MsgWindow_00")) return kPaneMsgWindow;
            return is("P_WindDirectionBase_00") ? kPaneWind : kPaneNone;
        case 'N':
            if (is("N_Finder_00")) return kPaneFinder;
            if (is("N_PictographAll_00")) return kPaneAlbum;
            return is("N_Zoom_00") ? kPaneZoom : kPaneNone;
        default: return kPaneNone;
    }
}
inline int zoom_kind(const char* first_child) {
    if (!strncmp(first_child, "P_Bar_00", 24)) return 1;
    if (!strncmp(first_child, "P_TelescopeBar_00", 24)) return 2;
    return 0;
}
// the PlayContext flag a pane's stamp stands for (0: it only helps another, as the cursor the choice)
inline uint32_t pane_signal_flag(PaneSignal s) {
    switch (s) {
        case kPaneKeyIcon: return kKeyIcon;
        case kPaneChoice2: return kChoice;
        case kPaneNumber: return kNumberEntry;
        case kPaneMsgWindow: return kMsgWindow;
        case kPaneFinder: return kPictoFinder;
        case kPaneAlbum: return kAlbum;
        case kPaneWind: return kWindCompass;
        case kPaneZoom: return kZoomBar;
        default: return 0;
    }
}
// pane stamps (the logic step + 1 each was last drawn on, 0 never) -> flags at logic step `now`; a
// choice needs its cursor too
inline uint32_t aged_pane_flags(const uint64_t (&seen)[kPaneSignalCount], uint64_t now) {
    uint32_t flags = 0;
    auto recent = [&](int i) { return seen[i] && now + 1 - seen[i] <= kPaneSteps; };
    for (int i = 0; i < kPaneSignalCount; i++)
        if (recent(i)) flags |= pane_signal_flag(PaneSignal(i));
    if (!recent(kPaneCursor)) flags &= ~kChoice;
    return flags;
}

// ---- the reader ----
// Mem: u8 / u16 / u32(addr) (big-endian guest reads) and object(addr, size) (the range is guest heap)
struct PaneSignals {  // from the Pane::Draw observer (play_context.cpp), already aged
    uint32_t flags = 0;  // kKeyIcon, kChoice, kNumberEntry, kMsgWindow, kPictoFinder, kAlbum, kWindCompass, kZoomBar
    uint8_t choice_count = 0, zoom_kind = 0;
    int16_t choice_y = 0, cursor_y = 0, msg_x = 0, msg_y = 0, msg_hw = 0, msg_hh = 0;
};
struct GameUiSignals {  // hud_gate.cpp's reading (mods::GameUi)
    bool event = false, message = false, menu = false;
};
struct Addresses {  // this build's addresses of the globals (play_context.cpp: GD())
    uint32_t game_info = 0;      // g_dComIfG_gameInfo (dComIfGp_get() returns it)
    uint32_t overlap = 0;        // l_fopOvlpM_overlap[0]: a fade / wipe runs (turbo.cpp)
    uint32_t save_info_ptr = 0;  // *this + 0x20: dSv_player_c (cheats.cpp)
};

// A Quest Log is played: a stage is loaded that is not the title screen ("sea_T") or the file select ("Name")
template <class Mem>
bool in_game(const Mem& m, const Addresses& a) {
    char stage[9] = {};
    for (int i = 0; i < 8; i++) stage[i] = char(m.u8(a.game_info + kStage + i));
    return stage[0] && strcmp(stage, "sea_T") != 0 && strcmp(stage, "Name") != 0;
}

// Link's actor if he is the controlled player, else 0
template <class Mem>
uint32_t link_actor(const Mem& m, const Addresses& a) {
    const uint32_t link = m.u32(a.game_info + kPlayer);
    return m.object(link, kLinkSize) ? link : 0;
}

// the dAttention_c Link keeps, if it belongs to him
template <class Mem>
uint32_t link_attention(const Mem& m, uint32_t link) {
    const uint32_t att = link ? m.u32(link + kLinkAttention) : 0;
    return m.object(att, 0xE0) && m.u32(att + kAttOwner) == link ? att : 0;
}

// The lock-on and its defeats: the locked target is remembered each step; when the lock ends and that
// enemy is dead (health 0 after having had health, deleted, or no longer lockable as an enemy) within a
// second, one defeat is counted. A lock ended by distance, an event or letting go of ZL keeps the count.
struct LockTracker {
    static constexpr int kWatchSteps = 30;  // 1 s at 30 steps per second
    uint32_t defeats = 0;
    uint32_t target = 0, target_id = 0;
    bool had_health = false;
    int watch = 0;  // steps left watching a target whose lock ended

    template <class Mem>
    void step(const Mem& m, uint32_t link, uint32_t att, PlayContext& out) {
        out.lock_state = 0;
        out.lock_reach = 0;
        if (!att) {
            target = 0;
            watch = 0;
            out.defeats = defeats;
            return;
        }
        const uint8_t st = m.u8(att + kAttLockState);
        const uint32_t t = m.u32(link + kLinkLockOnActor);
        const bool locked = st == 1 || (st == 2 && t);
        const uint32_t reach = m.u32(att + kAttLockCount);
        out.lock_state = st <= 2 ? st : 0;
        out.lock_reach = locked ? 0 : uint8_t(reach <= 8 ? reach : 0);
        if (locked) out.flags |= kLocked;
        out.defeats = defeats;
        if (locked && m.object(t, 0x3AC)) {
            if (m.u32(t + kActorAttnFlags) & kAttnLockonBattle) {
                if (t != target) had_health = false;
                watch = 0;
                target = t;
                target_id = m.u32(t + kActorProcId);
                had_health = had_health || int8_t(m.u8(t + kActorHealth)) > 0;  // it may reach 0 while still locked
            } else if (t != target) {
                target = 0;  // a person, a sign...
                watch = 0;
            }
            return;
        }
        if (!target || locked) return;  // (locked with Link's pointer empty for a step, around a hit: kept)
        // the lock ended: the enemy may still be dying, or deleted a few steps later (fpcBs_Delete clears mBsType)
        if (watch == 0) watch = kWatchSteps;
        const int8_t health = int8_t(m.u8(target + kActorHealth));
        const bool gone = m.u32(target + kActorBsType) == 0 || m.u32(target + kActorProcId) != target_id;
        const bool unlockable = !(m.u32(target + kActorAttnFlags) & kAttnLockonBattle);
        if (gone || (had_health && health <= 0) || unlockable) {
            out.defeats = ++defeats;
        } else if (--watch > 0) {
            return;
        }
        watch = 0;
        target = 0;
    }
};

// One reading. `ui` is hud_gate.cpp's (event, message, menu), `panes` the Pane::Draw observer's.
template <class Mem>
PlayContext read(const Mem& m, const Addresses& a, const GameUiSignals& ui, const PaneSignals& panes,
                 LockTracker& lock, uint64_t step) {
    PlayContext c;
    c.step = step;
    c.defeats = lock.defeats;  // also on the title screen: the count only grows
    const uint32_t g = a.game_info;
    if (m.u32(a.overlap) != 0) c.flags |= kFade;
    if (ui.event) c.flags |= kEvent;
    if (ui.message) c.flags |= kMessage;
    if (ui.menu) c.flags |= kMenu;
    c.flags |= panes.flags;
    c.choice_count = panes.choice_count;
    c.zoom_kind = panes.zoom_kind;
    c.choice_y = panes.choice_y;
    c.cursor_y = panes.cursor_y;
    c.msg_x = panes.msg_x;
    c.msg_y = panes.msg_y;
    c.msg_hw = panes.msg_hw;
    c.msg_hh = panes.msg_hh;
    if (!in_game(m, a)) {
        lock.step(m, 0, 0, c);
        return c;
    }
    c.flags |= kInGame;
    if (m.u8(g + kNextStageEnable)) c.flags |= kStageChange;
    c.mesg_status = m.u8(g + kMesgStatus);
    c.scope_mesg_status = m.u8(g + kScopeMesgStatus);
    c.zr_normal = m.u8(g + kRStatus);
    c.b_normal = m.u8(g + kAStatus);
    c.a_normal = m.u8(g + kDoStatus);
    c.zr_forced = m.u8(g + kRStatus + kForce);
    c.b_forced = m.u8(g + kAStatus + kForce);
    c.a_forced = m.u8(g + kDoStatus + kForce);
    // the HUD shows a forced action instead of the normal one (dMeter: Picto Box, album, telescope...)
    c.zr_action = c.zr_forced ? c.zr_forced : c.zr_normal;
    c.b_action = c.b_forced ? c.b_forced : c.b_normal;
    c.a_action = c.a_forced ? c.a_forced : c.a_normal;
    c.x_item = m.u8(g + kSelectItem);
    c.y_item = m.u8(g + kSelectItem + 1);
    c.r_item = m.u8(g + kSelectItem + 2);
    c.player_status0 = m.u32(g + kPlayerStatus0);
    c.player_status1 = m.u32(g + kPlayerStatus1);
    c.minigame = m.u8(g + kMiniGameType);
    // the inventory (the Wind Waker, the Swift Sail), from the save data when it looks loaded
    const uint32_t info = m.u32(a.save_info_ptr);
    if (m.object(info, 0x20 + kSaveInventory + 21)) {
        const uint32_t s = info + 0x20;
        const uint16_t max_life = m.u16(s + kSaveMaxLife), life = m.u16(s + kSaveLife);
        if (max_life >= 12 && max_life <= 80 && life <= max_life)
            for (uint32_t i = 0; i < 21; i++) {
                const uint8_t item = m.u8(s + kSaveInventory + i);
                if (item == kItemWindWaker) c.flags |= kHasWindWaker;
                if (item == kItemSwiftSail) c.flags |= kHasSwiftSail;
            }
    }
    const uint32_t link = link_actor(m, a);
    if (link) {
        c.flags |= kLink;
        c.proc = int32_t(m.u32(link + kLinkCurProc));
        c.mode_flags = m.u32(link + kLinkModeFlg);
        c.item_in_hand = m.u16(link + kLinkEquipItem);
        c.ready_item_button = m.u8(link + kLinkReadyItemBtn);
        const uint32_t entry = m.u32(link + kLinkAttnEntryA);
        if (m.object(entry, 0x10)) {
            const uint32_t type = m.u32(entry + kAttListType);
            c.attention_type = type <= kAttnShip ? uint8_t(type) : kAttnNone;
        }
    }
    lock.step(m, link, link_attention(m, link), c);
    // a shop's item choice: in a talk the game prompts A Choose / B Cancel without a choice box
    // (Beedle's shop, seen live: mesg status 14 while the stick picks an item)
    if ((c.flags & kEvent) && c.mesg_status && c.a_action == kActChoose && c.b_action == kActCancel &&
        !(c.flags & (kChoice | kNumberEntry)))
        c.flags |= kShop;
    // in play: the telescope's and the Picto Box's own message status (mScopeMesgStatus, set while
    // they are up) is no talk; hud_gate counts it as a message for its HUD fade only
    if (link && !c.mesg_status && !(c.flags & (kEvent | kMenu | kFade | kStageChange))) c.flags |= kInPlay;
    return c;
}

// ---- export to Java and other flat consumers (nativePlayContext) ----
// A fixed int array; the layout only grows (new fields at the end). Index 0 holds the version.
constexpr int kPackVersion = 1, kPackLen = 32;
inline void pack(const PlayContext& c, int32_t* out, int n) {
    int32_t v[kPackLen] = {
        kPackVersion, int32_t(c.flags), c.a_action, c.b_action, c.zr_action, c.x_item, c.y_item, c.r_item,
        int32_t(c.item_in_hand), c.proc, int32_t(c.player_status0), int32_t(c.player_status1), int32_t(c.mode_flags),
        c.minigame, c.ready_item_button, c.lock_state, c.lock_reach, c.attention_type, int32_t(c.defeats),
        c.mesg_status, c.scope_mesg_status, c.choice_count, c.zoom_kind, c.choice_y, c.cursor_y, c.msg_x, c.msg_y,
        c.msg_hw, c.msg_hh, c.a_normal | c.b_normal << 8 | c.zr_normal << 16, c.a_forced | c.b_forced << 8 | c.zr_forced << 16,
        int32_t(c.step & 0x7FFFFFFF),
    };
    for (int i = 0; i < n; i++) out[i] = i < kPackLen ? v[i] : 0;
}

// ---- the trace (WWHD_PLAYCTX_LOG): what changed is easy to see, one line per change ----
// equal for logging purposes: everything but the step and the drawn positions
inline bool same_for_log(const PlayContext& x, const PlayContext& y) {
    return x.flags == y.flags && x.a_action == y.a_action && x.b_action == y.b_action && x.zr_action == y.zr_action &&
           x.a_forced == y.a_forced && x.b_forced == y.b_forced && x.zr_forced == y.zr_forced && x.x_item == y.x_item &&
           x.y_item == y.y_item && x.r_item == y.r_item && x.minigame == y.minigame &&
           x.ready_item_button == y.ready_item_button && x.lock_state == y.lock_state && x.lock_reach == y.lock_reach &&
           x.attention_type == y.attention_type && x.mesg_status == y.mesg_status &&
           x.scope_mesg_status == y.scope_mesg_status && x.choice_count == y.choice_count && x.zoom_kind == y.zoom_kind &&
           x.item_in_hand == y.item_in_hand && x.proc == y.proc && x.player_status0 == y.player_status0 &&
           x.player_status1 == y.player_status1 && x.mode_flags == y.mode_flags && x.defeats == y.defeats;
}
inline int format(const PlayContext& c, char* buf, int size) {
    static const char* const kNames[] = {"game", "link", "play", "event", "message", "menu", "fade", "stagechange",
                                         "locked", "keyicon", "choice", "number", "msgwin", "finder", "album",
                                         "windcompass", "zoom", "shop", "windwaker", "swiftsail"};
    char names[200] = "";
    int k = 0;
    for (int i = 0; i < int(sizeof kNames / sizeof *kNames); i++)
        if (c.flags & (1u << i)) k += snprintf(names + k, sizeof names - k, "%s%s", k ? "," : "", kNames[i]);
    return snprintf(buf, size,
                    "step=%llu flags=%s A=%02X B=%02X ZR=%02X forced=%02X/%02X/%02X X=%02X Y=%02X R=%02X hand=%04X "
                    "proc=%d st0=%08X st1=%08X mode=%08X minigame=%u ready=%u lock=%u reach=%u attn=%u defeats=%u "
                    "mesg=%u/%u choice=%u zoom=%u",
                    (unsigned long long)c.step, names, c.a_action, c.b_action, c.zr_action, c.a_forced, c.b_forced,
                    c.zr_forced, c.x_item, c.y_item, c.r_item, c.item_in_hand, c.proc, c.player_status0, c.player_status1,
                    c.mode_flags, c.minigame, c.ready_item_button, c.lock_state, c.lock_reach, c.attention_type,
                    c.defeats, c.mesg_status, c.scope_mesg_status, c.choice_count, c.zoom_kind);
}

}  // namespace mods::playctx

namespace mods {
// ---- runtime (play_context.cpp) ----
// Once per full logic step on the game thread (interp.cpp, after a full pass began: true 60's half-pass
// preview is already taken back). No guest reads unless a consumer wants the context.
void play_context_step();
// a consumer (touch controls, the overlay, a test) wants the context read; reference-counted
void play_context_want(bool on);
// the last reading (any thread)
playctx::PlayContext play_context();
// nw::lyt::Pane::Draw (aspect.cpp hook_02877100): notes the message's parts, the Picto Box screens...
void play_context_pane(uint32_t pane);
}  // namespace mods
