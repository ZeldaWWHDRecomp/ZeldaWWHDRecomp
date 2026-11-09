// SPDX-License-Identifier: MPL-2.0
// Online co-op: the game structures' offsets the co-op code reads and writes, by name (GAPS C18).
//
// Layout facts only (no game data): each name is the member's name in this project's WWHD decompilation (wwhd_src,
// whose WWHD_OFFSET/WWHD_SIZE checks hold the offsets in the decomp build), in its header named per block. Where the
// decomp has no layout yet the block says "measured" and why the offset is believed (the function that uses it).
// File formats (BFRES, GX2 surfaces) follow their public descriptions.
#pragma once
#include <cstdint>

namespace mods::coop::names {

// The current stage's name as a clean 8-byte code: the name, then zeros. The game keeps it in a char[8] it fills with
// strcpy (dStage_nextStage_c::set), so after a longer name (M_NewD2, then sea) the old name's bytes stay after the
// NUL. The co-op messages compare and send all 8 bytes, and the server refuses a stage code with anything but zeros
// after its end (a pose with one is a protocol strike: 2026-11-07, a player kicked for "too many messages (pose)").
inline void clean_stage(const uint8_t* raw, char out[8]) {
    bool end = false;
    for (int i = 0; i < 8; i++) {
        const uint8_t ch = end ? 0 : raw[i];
        end = end || ch < 0x21 || ch > 0x7E;
        out[i] = end ? 0 : (char)ch;
    }
}

// dComIfG_inf_c (g_dComIfG_gameInfo, GD(0x1046F0B0) and dComIfGp_get 025200D4): offsets from its start
// (wwhd_src include/d/d_com_inf_game.h: dComIfG_play_c at 0x12A0, the PLAY_* enum)
namespace inf {
constexpr uint32_t kPlay = 0x12A0;
constexpr uint32_t kCcS = kPlay + 0x1404;                 // play.mCcS (PLAY_CCS)
constexpr uint32_t kCurStage = kPlay + 0x3E94;            // play.mCurStage (dStage_startStage_c: its name first)
constexpr uint32_t kNextStage = kPlay + 0x3EA0;           // play.mNextStage (dStage_nextStage_c)
constexpr uint32_t kNextStageEnabled = kNextStage + 0xC;  // dStage_nextStage_c::enabled (after the start stage, 0xC)
constexpr uint32_t kEvtCtrl = kPlay + 0x3F30;             // play.mEvtCtrl (PLAY_EVTCTRL)
constexpr uint32_t kEvtManager = kPlay + 0x4024;          // play.mEvtManager (PLAY_EVTMANAGER)
constexpr uint32_t kParticle = kPlay + 0x4810;            // play.mParticle (PLAY_PARTICLE)
constexpr uint32_t kPlayerInfo = kPlay + 0x488C;          // play.mPlayerInfo[0].mpPlayer (PLAY_PLAYER)
constexpr uint32_t kLink = kPlay + 0x4894;                // play.mpPlayerPtr[0] (PLAY_PLAYERPTR)
constexpr uint32_t kShip = kPlay + 0x4894 + 8;            // play.mpPlayerPtr[2] (dComIfGp_getShipActor)
constexpr uint32_t kItemRupeeCount = kPlay + 0x48A8;      // play.mItemRupeeCount
constexpr uint32_t kMesgStatus = kPlay + 0x4912;          // play.mMesgStatus
constexpr uint32_t kDrawlist = 0x5D30;                    // drawlist (dDlst_list_c, PLAY_DLST)
static_assert(kCcS == 0x26A4 && kCurStage == 0x5134 && kNextStage == 0x5140 && kEvtCtrl == 0x51D0 && kEvtManager == 0x52C4 &&
              kParticle == 0x5AB0 && kPlayerInfo == 0x5B2C && kLink == 0x5B34 && kShip == 0x5B3C && kMesgStatus == 0x5BB2 &&
              kItemRupeeCount == 0x5B48);
}  // namespace inf

// dEvt_control_c (wwhd_src include/d/d_event.h); dEvDtEvent_c (d_event_data.h: its name first, field_0x24 its kind)
namespace evt {
constexpr uint32_t mMode = 0xC2, mEventId = 0xD8;
constexpr uint32_t ev_field_0x24 = 0x24;
}

// dStage_roomControlData (wwhd_src include/d/d_stage.h: mStayNo at GD 0x1047E6C8, mStatus at +0x14) and
// dStage_roomStatus_c (0x22C each, mFlags +0x21C)
namespace rmc {
constexpr uint32_t kData = 0x1047E6B8, kStatus = kData + 0x14, kStatusSize = 0x22C, mFlags = 0x21C;
static_assert(kStatus + mFlags == 0x1047E8E8);
}

// dDlst_list_c (wwhd_src include/d/d_drawlist.h, every member held by WWHD_OFFSET)
namespace dlst {
constexpr uint32_t mpOpaListP1 = 0x28, mpOpaListPlayerExtra = 0x30, mpOpaListZelda0 = 0x34, mpOpaListZelda1 = 0x38,
                   mpXluListZelda1 = 0x3C, mpXluList = 0x4C, m3DlineMatSortPacket = 0x284;
// measured: the line packets in m3DlineMatSortPacket are 0x9C bytes, each's first line (mDoExt_3DlineMat_c, linked by
// its +0) at +0x98 (the rope's packet is the second, as daHimo2's draw 025EDD04 takes it)
constexpr uint32_t kLinePacket = 0x9C, kLinePacketFirst = 0x98;
}

// base_process_class (wwhd_src include/f_pc/f_pc_base.h): its layer tag's layer (mLyTg 0x18, its mpLayer +0x14)
namespace bpc {
constexpr uint32_t mBsPcId = 0x04, mProcName = 0x08, kLayer = 0x18 + 0x14;
}

// fopAc_ac_c (wwhd_src include/f_op/f_op_actor.h)
namespace ac {
constexpr uint32_t tevStr = 0x110, old = 0x300, current = 0x314, shape_angle = 0x328, speed = 0x33C, speedF = 0x370,
                   gravity = 0x374, maxFallSpeed = 0x378, health = 0x3A1;
constexpr uint32_t kPos = current, kRoom = current + 0x12;  // actor_place pos, roomNo
constexpr uint32_t kOldPos = old;
constexpr uint32_t kAngleY = shape_angle + 2;               // shape_angle.y
constexpr uint32_t kSpeedY = speed + 4;
// dKy_tevstr_c's fog: mFogColor (GXColorS10) +0xA0, mFogStartZ +0xA8, mFogEndZ +0xAC
constexpr uint32_t kFogColor = tevStr + 0xA0, kFogStart = tevStr + 0xA8, kFogEnd = tevStr + 0xAC;
}  // namespace ac

// daPy_py_c / daPy_lk_c (wwhd_src include/d/actor/d_a_player.h, d_a_player_main.h)
namespace py {
constexpr uint32_t mDamageWaitTimer = 0x3B0;  // daPy_py_c
constexpr uint32_t mpCLModel = 0x448, mpZOffBlendShape = 0x60C, mpZOffNoneShape = 0x61C, mpZOnShape = 0x62C;
constexpr uint32_t mAcch = 0x80C;
constexpr uint32_t kAcchLin = mAcch + 0x40;  // dBgS_Acch::m_lin (wwhd_src include/d/d_bg_s.h)
// dBgS_Acch::m_gnd (+0xD4), its cBgS_PolyInfo (+0x14): the ground polygon (s16), then its body (dBgW*) at +4
constexpr uint32_t kGndPoly = mAcch + 0xD4 + 0x14, kGndBg = kGndPoly + 4;
// measured (d_a_player_main.h: "brk/btk animations 0x44D4..", the bomb's brk as daBomb_c::draw_norm 020C4398 enters it;
// its frame at +4)
constexpr uint32_t kBombBrk = 0x44D4;
constexpr uint32_t mCurProc = 0x65F0, mLightInfluence = 0x68B0, mItemTrigger = 0x690D, mItemButton = 0x690E,
                   mEquipItem = 0x69B0, m35F0 = 0x6A48, m35F4 = 0x6A4C, mModeFlg = 0x6A70, m3688 = 0x7290,
                   mHookshotRootPos = 0x72F0, m3748 = 0x7350, mpSwBlur = 0x73EC, mStts = 0x7620, mCyl = 0x765C;
constexpr uint32_t mStickDistance = 0x6A08;  // (the stick's tilt as Link's execute took it, 0..1: setStickData)
constexpr uint32_t kFallStartY = m3688 + 4;  // m3688.y, with m35F0 and m35F4 as procAutoJump_init sets them
constexpr uint32_t kSize = 0x8284;           // WWHD_SIZE(daPy_lk_c)
}  // namespace py

// dCcD_GObjInf / dCcD_Stts (wwhd_src include/d/d_cc_d.h), as coop_monsters names them
namespace cc {
constexpr uint32_t mObjTg_mRPrm = 0x1C, mGObjTg = 0x94, tg_mRPrm = mGObjTg + 0x04, tg_mRVec = mGObjTg + 0x2C;
constexpr uint32_t stts_m_dmg = 0x16;
}

// daPy_swBlur_c (wwhd_src include/d/actor/d_a_player_main_04b.h)
namespace swblur {
constexpr uint32_t mPointCount = 0x9C, mSampleIndex = 0xA0, mBlurColorType = 0xA4, mBlurTopRate = 0xA8, mpPosBuffer = 0xAC,
                   mTopPos = 0xBC, mBottomPos = 0x38C;
constexpr uint32_t kSize = 0x1243C;  // as daPy_lk_c's createHeap allocates it for 0240F564
}

// daHookshot_c (wwhd_src include/d/actor/d_a_hookshot.h)
namespace hookshot {
constexpr uint32_t mChainCnt = 0xD504;
}
// daHimo2_c (wwhd_src include/d/actor/d_a_himo2.h): its line m1F30 (himo2_lineMat_c at 0x204C) and the count m1F6C
namespace himo2 {
constexpr uint32_t m1F30 = 0x204C, m1F6C = 0x21D4;
}
// daBomb_c (wwhd_src include/d/actor/d_a_bomb.h)
namespace bomb {
constexpr uint32_t mType = 0x5C8;
}
// mDoExt_3DlineMat1_c (wwhd_src include/m_Do/m_Do_ext_line_mat1.h)
namespace linemat {
constexpr uint32_t mpLines = 0x184;
}
// mDoExt_McaMorf (wwhd_src include/m_Do/m_Do_ext.h)
namespace mca {
constexpr uint32_t mpModel = 0x90;
}

// dBgW (wwhd_src include/d/d_bg_w.h)
namespace bgw {
constexpr uint32_t m_crr_func = 0xA8, mRoomId = 0xB8, mGrpRoomInf = 0xBB;
}

// J3D (wwhd_src include/JSystem/J3DGraph.h): J3DModel, J3DModelData, J3DMtxBuffer_hd, J3DJoint, J3DMaterial
namespace j3d {
// J3DModel
constexpr uint32_t mpMtxBuffer = 0x2C, mModelData = 0xAC, mBaseTransformMtx = 0xC8;
// J3DModel, measured: the shape packets' draw flags (027F596C sets them from it), the material packets' count and
// array (J3DModel's texture list 027F50F8 walks them; 0x3C each, its texture list +0x2C, the material +0 whose texture
// count is at +0x11)
constexpr uint32_t kDiffFlag = 0x74, kMatPacketNum = 0x2A, kMatPackets = 0x34, kMatPacket = 0x3C, kMatPacketTex = 0x2C,
                   kMatTexNum = 0x11;
// J3DMtxBuffer_hd: the joint matrices (0x30 each); measured: their count at +0x2C
constexpr uint32_t mpAnmMtx = 0x10, kJointCount = 0x2C;
// J3DModelData
constexpr uint32_t mJointNodes = 0x08, mMaterialNum = 0x0C, mMaterials = 0x10;
constexpr uint32_t kJoint = 0x1C, kMaterial = 0x39C;  // getJointNodePointer / getMaterialNodePointer strides
// J3DModelData, measured: the texture table (a u16 count, the entries' array at +4, 0x24 each, the texture object +0x20)
constexpr uint32_t kTexTable = 0x30, kTexTableEntries = 4, kTexEntry = 0x24, kTexEntryObj = 0x20;
// J3DMaterial: mShape (J3DShape: mVisible +4); measured: a joint's first material at the joint's +0x10, the next at a
// material's +4
constexpr uint32_t mShape = 0x08, mVisible = 0x04, kJointMaterial = 0x10, kMaterialNext = 0x04;
}  // namespace j3d

// The heaps a kit or a held model is made on (sead::FrameHeap behind JKRSolidHeap); measured, no WWHD layout yet: the
// flags +0x90 (bit 2 cleared as JKRSolidHeap::create leaves it), the free part from +0x94 to +0x98; the vtable +0xC,
// getHeapSize its +0x6C (gc_api/JSystem/JKernel/JKRSolidHeap.h, WWHD_OFFSET)
namespace sheap {
constexpr uint32_t kFlags = 0x90, kHead = 0x94, kTail = 0x98, kVtable = 0xC, kGetHeapSize = 0x6C;
}

// A texture object (the game's: a GX2Texture at +4) and its GX2Surface (the public Cafe SDK layout)
namespace gx2 {
constexpr uint32_t kTexSurface = 4;
constexpr uint32_t width = 0x04, height = 0x08, format = 0x14, imageSize = 0x20, image = 0x24, mipmapSize = 0x28,
                   mipmaps = 0x2C, alignment = 0x38;
constexpr uint32_t kTexObject = 0xC0;  // the object's size, as the clothes' copies take it
}

// BFRES (the public format description): FMDL, FSKL, a bone, an index group. Offsets "rel" are self-relative.
namespace bfres {
constexpr uint32_t fmdl_skeleton = 0x0C, fmdl_materials = 0x18;  // rel: FSKL, FMAT index group
constexpr uint32_t fskl_bone_count = 0x08, fskl_bones = 0x14;    // u16; rel: the bone array
constexpr uint32_t bone_name = 0x00, bone_index = 0x04, bone_parent = 0x06, bone_flags = 0x10, bone_rotation = 0x20,
                   bone_translation = 0x30;                       // rel name; u16 index, s16 parent; f32 x4, x3
constexpr uint32_t kFskl = 0x46534B4C;                            // "FSKL"
constexpr uint32_t group_count = 0x04, group_entries = 0x08, group_entry = 0x10, entry_name = 0x08;  // entry 0: the root
}

}  // namespace mods::coop::names
