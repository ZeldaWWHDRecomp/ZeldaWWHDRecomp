// SPDX-License-Identifier: MPL-2.0
// World editor: names for the demo step's sound and particle pickers (demo_names.h). The ids are the
// game's sound effect and particle ids; the names are the source-code names the decompilation uses for
// them (no game data), with our own group words. A sound or particle not listed here can still be
// entered by its number.
#include "demo_names.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "../mods/demo_scene.h"

namespace editor {
namespace {
const NamedId kSounds[] = {
    {0x0806, "effect", "riddle 1", "JA_SE_READ_RIDDLE_1"},
    {0x0834, "effect", "alerm", "JA_SE_MAJUTOU_ALERM"},
    {0x083E, "effect", "sw on", "JA_SE_SHOCK_SW_ON"},
    {0x085B, "battle", "ji opening", "JA_SE_VS_JI_OPENING"},
    {0x085C, "battle", "ji ending", "JA_SE_VS_JI_ENDING"},
    {0x0872, "Wind Waker", "wind disp", "JA_SE_TAKT_WIND_DISP"},
    {0x0873, "Wind Waker", "wind cancel", "JA_SE_TAKT_WIND_CANCEL"},
    {0x0875, "Wind Waker", "wind decide", "JA_SE_TAKT_WIND_DECIDE"},
    {0x0876, "Wind Waker", "wind demo", "JA_SE_TAKT_WIND_DEMO"},
    {0x0877, "Wind Waker", "wind end", "JA_SE_TAKT_WIND_END"},
    {0x0905, "effect", "mo check", "JA_SE_MAJU_MO_CHECK"},
    {0x105F, "ambience", "rakuban", "JA_SE_ATM_RAKUBAN"},
    {0x106A, "ambience", "wind var", "JA_SE_ATM_WIND_VAR"},
    {0x106B, "object", "tornade sus", "JA_SE_OBJ_TORNADE_SUS"},
    {0x1074, "system", "wtakt wind amb", "JA_SE_SYS_WTAKT_WIND_AMB"},
    {0x201F, "Link", "rope unwind", "JA_SE_LK_ROPE_UNWIND"},
    {0x206C, "Link", "hs chain", "JA_SE_LK_HS_CHAIN"},
    {0x206D, "Link", "hs wind up", "JA_SE_LK_HS_WIND_UP"},
    {0x2803, "Link", "sw hit s", "JA_SE_LK_SW_HIT_S"},
    {0x2806, "Link", "sw crt hit", "JA_SE_LK_SW_CRT_HIT"},
    {0x2817, "Link", "rope launch", "JA_SE_LK_ROPE_LAUNCH"},
    {0x2818, "Link", "rope coil 1", "JA_SE_LK_ROPE_COIL_1"},
    {0x2819, "Link", "rope coil 2", "JA_SE_LK_ROPE_COIL_2"},
    {0x281A, "Link", "rope coil 3", "JA_SE_LK_ROPE_COIL_3"},
    {0x281B, "Link", "rope hook wood", "JA_SE_LK_ROPE_HOOK_WOOD"},
    {0x281D, "Link", "rope hook metal", "JA_SE_LK_ROPE_HOOK_METAL"},
    {0x2820, "Link", "rope maxlength", "JA_SE_LK_ROPE_MAXLENGTH"},
    {0x2821, "Link", "rope uncoil", "JA_SE_LK_ROPE_UNCOIL"},
    {0x2828, "Link", "last hit", "JA_SE_LK_LAST_HIT"},
    {0x2830, "Link", "rope hook body", "JA_SE_LK_ROPE_HOOK_BODY"},
    {0x2833, "Link", "w wep hit", "JA_SE_LK_W_WEP_HIT"},
    {0x2834, "Link", "ms wep hit", "JA_SE_LK_MS_WEP_HIT"},
    {0x2835, "Link", "w wep crt hit", "JA_SE_LK_W_WEP_CRT_HIT"},
    {0x2839, "Link", "hit sbridge chain", "JA_SE_LK_HIT_SBRIDGE_CHAIN"},
    {0x2842, "Link", "rope swing round", "JA_SE_LK_ROPE_SWING_ROUND"},
    {0x2855, "Link", "hammer hit", "JA_SE_LK_HAMMER_HIT"},
    {0x286E, "Link", "hs wind up fin", "JA_SE_LK_HS_WIND_UP_FIN"},
    {0x286F, "Link", "hs spike", "JA_SE_LK_HS_SPIKE"},
    {0x2879, "Link", "arrow hit", "JA_SE_LK_ARROW_HIT"},
    {0x287B, "Link", "hs rebound", "JA_SE_LK_HS_REBOUND"},
    {0x287D, "Link", "boss warp eff st", "JA_SE_LK_BOSS_WARP_EFF_ST"},
    {0x2884, "Link", "flift go water", "JA_SE_LK_FLIFT_GO_WATER"},
    {0x2889, "Link", "warp to g ship", "JA_SE_LK_WARP_TO_G_SHIP"},
    {0x28A5, "Link", "pg bomb strike", "JA_SE_LK_PG_BOMB_STRIKE"},
    {0x3823, "object", "shoku lift move", "JA_SE_OBJ_SHOKU_LIFT_MOVE"},
    {0x4805, "voice", "kamome", "JA_SE_CV_KAMOME"},
    {0x4809, "voice", "mo damage", "JA_SE_CV_MO_DAMAGE"},
    {0x480A, "voice", "mo damage hip", "JA_SE_CV_MO_DAMAGE_HIP"},
    {0x480D, "voice", "mo jab", "JA_SE_CV_MO_JAB"},
    {0x480E, "voice", "mo fainted", "JA_SE_CV_MO_FAINTED"},
    {0x480F, "voice", "mo find enemy", "JA_SE_CV_MO_FIND_ENEMY"},
    {0x4810, "voice", "mo lose lance", "JA_SE_CV_MO_LOSE_LANCE"},
    {0x4811, "voice", "mo find lance", "JA_SE_CV_MO_FIND_LANCE"},
    {0x4812, "voice", "mo search", "JA_SE_CV_MO_SEARCH"},
    {0x4813, "voice", "bb normal", "JA_SE_CV_BB_NORMAL"},
    {0x4814, "voice", "bb find", "JA_SE_CV_BB_FIND"},
    {0x4815, "voice", "bb attack", "JA_SE_CV_BB_ATTACK"},
    {0x4816, "voice", "bb damage", "JA_SE_CV_BB_DAMAGE"},
    {0x4817, "voice", "bb fainted", "JA_SE_CV_BB_FAINTED"},
    {0x4818, "voice", "bb furafura", "JA_SE_CV_BB_FURAFURA"},
    {0x481C, "voice", "ki wakeup", "JA_SE_CV_KI_WAKEUP"},
    {0x481D, "voice", "ki attack", "JA_SE_CV_KI_ATTACK"},
    {0x481E, "voice", "ki damage", "JA_SE_CV_KI_DAMAGE"},
    {0x4820, "voice", "pg normal", "JA_SE_CV_PG_NORMAL"},
    {0x4821, "voice", "pg turn", "JA_SE_CV_PG_TURN"},
    {0x4822, "voice", "pg catch", "JA_SE_CV_PG_CATCH"},
    {0x4823, "voice", "pg carry", "JA_SE_CV_PG_CARRY"},
    {0x4826, "voice", "bk attack l", "JA_SE_CV_BK_ATTACK_L"},
    {0x482A, "voice", "bk search", "JA_SE_CV_BK_SEARCH"},
    {0x482B, "voice", "bk found link", "JA_SE_CV_BK_FOUND_LINK"},
    {0x482C, "voice", "bk lost boko", "JA_SE_CV_BK_LOST_BOKO"},
    {0x482D, "voice", "bk search boko", "JA_SE_CV_BK_SEARCH_BOKO"},
    {0x482E, "voice", "bk found boko", "JA_SE_CV_BK_FOUND_BOKO"},
    {0x482F, "voice", "bk jump", "JA_SE_CV_BK_JUMP"},
    {0x4830, "voice", "bk surprise", "JA_SE_CV_BK_SURPRISE"},
    {0x4831, "voice", "bk nobi", "JA_SE_CV_BK_NOBI"},
    {0x4832, "voice", "mo alert", "JA_SE_CV_MO_ALERT"},
    {0x4833, "voice", "ji defence", "JA_SE_CV_JI_DEFENCE"},
    {0x4833, "voice", "ji defence", "JA_SE_CV_JI_DEFENCE_"},
    {0x483F, "voice", "drg rockfall 1", "JA_SE_CV_DRG_ROCKFALL_1"},
    {0x4840, "voice", "drg rockfall 2", "JA_SE_CV_DRG_ROCKFALL_2"},
    {0x4841, "voice", "drg set rope", "JA_SE_CV_DRG_SET_ROPE"},
    {0x4843, "voice", "ji attack", "JA_SE_CV_JI_ATTACK"},
    {0x4844, "voice", "drg rockfall 1 2", "JA_SE_CV_DRG_ROCKFALL_1_2"},
    {0x4845, "voice", "drg rockfall 1 3", "JA_SE_CV_DRG_ROCKFALL_1_3"},
    {0x4860, "voice", "cc stand up", "JA_SE_CV_CC_STAND_UP"},
    {0x4861, "voice", "cc attack", "JA_SE_CV_CC_ATTACK"},
    {0x4862, "voice", "cc damage", "JA_SE_CV_CC_DAMAGE"},
    {0x4863, "voice", "cc die", "JA_SE_CV_CC_DIE"},
    {0x486D, "voice", "ks attack", "JA_SE_CV_KS_ATTACK"},
    {0x486E, "voice", "ks damage", "JA_SE_CV_KS_DAMAGE"},
    {0x486F, "voice", "ks blow", "JA_SE_CV_KS_BLOW"},
    {0x4874, "voice", "am awake", "JA_SE_CV_AM_AWAKE"},
    {0x4875, "voice", "am jump", "JA_SE_CV_AM_JUMP"},
    {0x4876, "voice", "am open mouth", "JA_SE_CV_AM_OPEN_MOUTH"},
    {0x4877, "voice", "am eye damage", "JA_SE_CV_AM_EYE_DAMAGE"},
    {0x4878, "voice", "am eat bomb", "JA_SE_CV_AM_EAT_BOMB"},
    {0x4879, "voice", "am jitabata", "JA_SE_CV_AM_JITABATA"},
    {0x48B1, "voice", "fm damage", "JA_SE_CV_FM_DAMAGE"},
    {0x48B2, "voice", "fm die", "JA_SE_CV_FM_DIE"},
    {0x48BB, "voice", "cc parasite", "JA_SE_CV_CC_PARASITE"},
    {0x48D3, "voice", "rd scream", "JA_SE_CV_RD_SCREAM"},
    {0x48D4, "voice", "rd damage", "JA_SE_CV_RD_DAMAGE"},
    {0x48D5, "voice", "rd die", "JA_SE_CV_RD_DIE"},
    {0x48E3, "voice", "bwd c hs damage", "JA_SE_CV_BWD_C_HS_DAMAGE"},
    {0x48E4, "voice", "bwd c attack", "JA_SE_CV_BWD_C_ATTACK"},
    {0x48E5, "voice", "bwd c damage", "JA_SE_CV_BWD_C_DAMAGE"},
    {0x48E6, "voice", "bwd c die", "JA_SE_CV_BWD_C_DIE"},
    {0x48F7, "voice", "pt land", "JA_SE_CV_PT_LAND"},
    {0x48F8, "voice", "pt jump", "JA_SE_CV_PT_JUMP"},
    {0x48F9, "voice", "pt damage", "JA_SE_CV_PT_DAMAGE"},
    {0x48FA, "voice", "pt die", "JA_SE_CV_PT_DIE"},
    {0x48FB, "voice", "pt koke", "JA_SE_CV_PT_KOKE"},
    {0x48FC, "voice", "pt koke2", "JA_SE_CV_PT_KOKE2"},
    {0x4900, "voice", "mo stolen", "JA_SE_CV_MO_STOLEN"},
    {0x4915, "voice", "pg l normal", "JA_SE_CV_PG_L_NORMAL"},
    {0x4916, "voice", "pg l turn", "JA_SE_CV_PG_L_TURN"},
    {0x4917, "voice", "pg l catch", "JA_SE_CV_PG_L_CATCH"},
    {0x4918, "voice", "pg l carry", "JA_SE_CV_PG_L_CARRY"},
    {0x4925, "voice", "g ship laugh", "JA_SE_CV_G_SHIP_LAUGH"},
    {0x4926, "voice", "g ship scream", "JA_SE_CV_G_SHIP_SCREAM"},
    {0x4932, "voice", "ji futtobi", "JA_SE_CV_JI_FUTTOBI"},
    {0x5021, "creature", "ji slip", "JA_SE_CM_JI_SLIP"},
    {0x50BC, "creature", "md piyo", "JA_SE_CM_MD_PIYO"},
    {0x5127, "creature", "sh propeller", "JA_SE_CM_SH_PROPELLER"},
    {0x5135, "creature", "pw become solid", "JA_SE_CM_PW_BECOME_SOLID"},
    {0x5801, "creature", "mons explode", "JA_SE_CM_MONS_EXPLODE"},
    {0x5806, "creature", "lance pickup", "JA_SE_CM_LANCE_PICKUP"},
    {0x5808, "creature", "lance hit floor", "JA_SE_CM_LANCE_HIT_FLOOR"},
    {0x5822, "creature", "ji attack", "JA_SE_CM_JI_ATTACK"},
    {0x5824, "creature", "ji damage", "JA_SE_CM_JI_DAMAGE"},
    {0x5827, "creature", "btd rope set", "JA_SE_CM_BTD_ROPE_SET"},
    {0x5828, "creature", "btd stn fall", "JA_SE_CM_BTD_STN_FALL"},
    {0x5829, "creature", "btd bef rock fall", "JA_SE_CM_BTD_BEF_ROCK_FALL"},
    {0x582A, "creature", "btd rock fall", "JA_SE_CM_BTD_ROCK_FALL"},
    {0x582B, "creature", "btd rock hit", "JA_SE_CM_BTD_ROCK_HIT"},
    {0x582F, "creature", "btd rock attach", "JA_SE_CM_BTD_ROCK_ATTACH"},
    {0x5836, "creature", "bk bb landing", "JA_SE_CM_BK_BB_LANDING"},
    {0x5843, "creature", "bv base popup", "JA_SE_CM_BV_BASE_POPUP"},
    {0x5857, "creature", "ph propeller", "JA_SE_CM_PH_PROPELLER"},
    {0x585D, "creature", "cc enter gnd", "JA_SE_CM_CC_ENTER_GND"},
    {0x585E, "creature", "cc landing", "JA_SE_CM_CC_LANDING"},
    {0x585F, "creature", "cc stand to lie", "JA_SE_CM_CC_STAND_TO_LIE"},
    {0x5860, "creature", "cc lie to stand", "JA_SE_CM_CC_LIE_TO_STAND"},
    {0x5861, "creature", "cc move lie", "JA_SE_CM_CC_MOVE_LIE"},
    {0x5862, "creature", "cc move stand", "JA_SE_CM_CC_MOVE_STAND"},
    {0x5863, "creature", "cc attack", "JA_SE_CM_CC_ATTACK"},
    {0x5864, "creature", "cc dmg swing", "JA_SE_CM_CC_DMG_SWING"},
    {0x5865, "creature", "cc die swing", "JA_SE_CM_CC_DIE_SWING"},
    {0x5866, "creature", "cc blue spark", "JA_SE_CM_CC_BLUE_SPARK"},
    {0x5868, "object", "vine s recover", "JA_SE_OBJ_VINE_S_RECOVER"},
    {0x587A, "creature", "ks move", "JA_SE_CM_KS_MOVE"},
    {0x587B, "creature", "ks attack", "JA_SE_CM_KS_ATTACK"},
    {0x587C, "creature", "ks die", "JA_SE_CM_KS_DIE"},
    {0x587D, "creature", "am needle out", "JA_SE_CM_AM_NEEDLE_OUT"},
    {0x587E, "creature", "am needle in", "JA_SE_CM_AM_NEEDLE_IN"},
    {0x587F, "creature", "am jump", "JA_SE_CM_AM_JUMP"},
    {0x5880, "creature", "am mouth open", "JA_SE_CM_AM_MOUTH_OPEN"},
    {0x5881, "creature", "am eye damage", "JA_SE_CM_AM_EYE_DAMAGE"},
    {0x5882, "creature", "am jump s", "JA_SE_CM_AM_JUMP_S"},
    {0x5883, "creature", "am mouth close", "JA_SE_CM_AM_MOUTH_CLOSE"},
    {0x5884, "creature", "am eat bomb", "JA_SE_CM_AM_EAT_BOMB"},
    {0x5885, "creature", "am jump l", "JA_SE_CM_AM_JUMP_L"},
    {0x5886, "creature", "am bef explode", "JA_SE_CM_AM_BEF_EXPLODE"},
    {0x5887, "creature", "am explode", "JA_SE_CM_AM_EXPLODE"},
    {0x58A0, "creature", "ji shotei", "JA_SE_CM_JI_SHOTEI"},
    {0x58A1, "creature", "aj angry foot", "JA_SE_CM_AJ_ANGRY_FOOT"},
    {0x58AD, "creature", "fm grab", "JA_SE_CM_FM_GRAB"},
    {0x58B1, "creature", "fm grab hand", "JA_SE_CM_FM_GRAB_HAND"},
    {0x58F4, "creature", "mo bb landing", "JA_SE_CM_MO_BB_LANDING"},
    {0x58F7, "object", "svine out", "JA_SE_OBJ_SVINE_OUT"},
    {0x58F8, "object", "svine out water", "JA_SE_OBJ_SVINE_OUT_WATER"},
    {0x58F9, "object", "svine grasp", "JA_SE_OBJ_SVINE_GRASP"},
    {0x58FB, "creature", "cc stoned", "JA_SE_CM_CC_STONED"},
    {0x58FC, "creature", "cc stoned recover", "JA_SE_CM_CC_STONED_RECOVER"},
    {0x58FE, "object", "ostatue put", "JA_SE_OBJ_OSTATUE_PUT"},
    {0x591E, "creature", "b cc recover", "JA_SE_CM_B_CC_RECOVER"},
    {0x5921, "creature", "pg dig", "JA_SE_CM_PG_DIG"},
    {0x5922, "creature", "pg dig landing", "JA_SE_CM_PG_DIG_LANDING"},
    {0x592D, "object", "pt jump", "JA_SE_OBJ_PT_JUMP"},
    {0x592E, "object", "pt land", "JA_SE_OBJ_PT_LAND"},
    {0x592F, "object", "pt fall", "JA_SE_OBJ_PT_FALL"},
    {0x593E, "creature", "pg l jump", "JA_SE_CM_PG_L_JUMP"},
    {0x593F, "creature", "pg l dig", "JA_SE_CM_PG_L_DIG"},
    {0x594C, "creature", "ji swing", "JA_SE_CM_JI_SWING"},
    {0x5970, "creature", "bgn d string plink", "JA_SE_CM_BGN_D_STRING_PLINK"},
    {0x6103, "object", "torch burning", "JA_SE_OBJ_TORCH_BURNING"},
    {0x612E, "object", "steam", "JA_SE_OBJ_STEAM"},
    {0x6165, "object", "propeller spin", "JA_SE_OBJ_PROPELLER_SPIN"},
    {0x6166, "object", "ami prop", "JA_SE_OBJ_AMI_PROP"},
    {0x619A, "object", "tc tower round", "JA_SE_OBJ_TC_TOWER_ROUND"},
    {0x61AB, "creature", "drg mtop fire", "JA_SE_CM_DRG_MTOP_FIRE"},
    {0x61B1, "object", "propeller rot", "JA_SE_OBJ_PROPELLER_ROT"},
    {0x61E4, "creature", "bwd c move sand", "JA_SE_CM_BWD_C_MOVE_SAND"},
    {0x61FE, "object", "spring", "JA_SE_OBJ_SPRING"},
    {0x6817, "object", "col sws nmtlp", "JA_SE_OBJ_COL_SWS_NMTLP"},
    {0x6820, "object", "col swc nstone", "JA_SE_OBJ_COL_SWC_NSTONE"},
    {0x6902, "object", "torch ignition", "JA_SE_OBJ_TORCH_IGNITION"},
    {0x6904, "object", "torch off", "JA_SE_OBJ_TORCH_OFF"},
    {0x6918, "object", "fall water s", "JA_SE_OBJ_FALL_WATER_S"},
    {0x6919, "object", "fall water m", "JA_SE_OBJ_FALL_WATER_M"},
    {0x691A, "object", "fall water l", "JA_SE_OBJ_FALL_WATER_L"},
    {0x691C, "object", "fall magma m", "JA_SE_OBJ_FALL_MAGMA_M"},
    {0x691D, "object", "fall magma l", "JA_SE_OBJ_FALL_MAGMA_L"},
    {0x6929, "object", "put stone", "JA_SE_OBJ_PUT_STONE"},
    {0x692B, "object", "break stone", "JA_SE_OBJ_BREAK_STONE"},
    {0x692C, "object", "break rock", "JA_SE_OBJ_BREAK_ROCK"},
    {0x692D, "object", "break steps", "JA_SE_OBJ_BREAK_STEPS"},
    {0x6937, "object", "break board", "JA_SE_OBJ_BREAK_BOARD"},
    {0x6940, "object", "btd board burn", "JA_SE_OBJ_BTD_BOARD_BURN"},
    {0x6941, "object", "btd board brk", "JA_SE_OBJ_BTD_BOARD_BRK"},
    {0x6943, "object", "wpot liftup", "JA_SE_OBJ_WPOT_LIFTUP"},
    {0x6946, "object", "jflower in", "JA_SE_OBJ_JFLOWER_IN"},
    {0x6947, "object", "jflower out", "JA_SE_OBJ_JFLOWER_OUT"},
    {0x694B, "object", "jama shoku out", "JA_SE_OBJ_JAMA_SHOKU_OUT"},
    {0x694D, "object", "jama shoku in", "JA_SE_OBJ_JAMA_SHOKU_IN"},
    {0x6951, "object", "kokiri h landing", "JA_SE_OBJ_KOKIRI_H_LANDING"},
    {0x6953, "object", "p flower land w", "JA_SE_OBJ_P_FLOWER_LAND_W"},
    {0x6954, "object", "ladder fall 1", "JA_SE_OBJ_LADDER_FALL_1"},
    {0x6955, "object", "ladder fall 2", "JA_SE_OBJ_LADDER_FALL_2"},
    {0x6956, "object", "l bridge on", "JA_SE_OBJ_L_BRIDGE_ON"},
    {0x6957, "object", "l bridge off", "JA_SE_OBJ_L_BRIDGE_OFF"},
    {0x695E, "object", "arrow sw on", "JA_SE_OBJ_ARROW_SW_ON"},
    {0x6961, "object", "mirror reflect", "JA_SE_OBJ_MIRROR_REFLECT"},
    {0x6968, "object", "mj wboard brk", "JA_SE_OBJ_MJ_WBOARD_BRK"},
    {0x6976, "object", "toge out", "JA_SE_OBJ_TOGE_OUT"},
    {0x6977, "object", "toge in", "JA_SE_OBJ_TOGE_IN"},
    {0x698F, "creature", "bst warp beam", "JA_SE_CM_BST_WARP_BEAM"},
    {0x69A4, "object", "tc jail door op", "JA_SE_OBJ_TC_JAIL_DOOR_OP"},
    {0x69AC, "object", "bomb dn st s", "JA_SE_OBJ_BOMB_DN_ST_S"},
    {0x69AD, "object", "bomb dn st e", "JA_SE_OBJ_BOMB_DN_ST_E"},
    {0x69B0, "object", "svine crash", "JA_SE_OBJ_SVINE_CRASH"},
    {0x69B4, "object", "b shutter open", "JA_SE_OBJ_B_SHUTTER_OPEN"},
    {0x69B5, "object", "b shutter stop", "JA_SE_OBJ_B_SHUTTER_STOP"},
    {0x69BB, "object", "sw stair on 1", "JA_SE_OBJ_SW_STAIR_ON_1"},
    {0x69BC, "object", "sw stair off 1", "JA_SE_OBJ_SW_STAIR_OFF_1"},
    {0x69BD, "object", "msdan sub2", "JA_SE_OBJ_MSDAN_SUB2"},
    {0x69BF, "object", "tenmado shtr op", "JA_SE_OBJ_TENMADO_SHTR_OP"},
    {0x69C3, "object", "heavy floor brk", "JA_SE_OBJ_HEAVY_FLOOR_BRK"},
    {0x69CA, "object", "wdun r04 str op", "JA_SE_OBJ_WDUN_R04_STR_OP"},
    {0x69D2, "object", "rope sw on", "JA_SE_OBJ_ROPE_SW_ON"},
    {0x69D3, "object", "st chime", "JA_SE_OBJ_ST_CHIME"},
    {0x69DD, "object", "mini ice", "JA_SE_OBJ_MINI_ICE"},
    {0x69DE, "object", "mini ice break", "JA_SE_OBJ_MINI_ICE_BREAK"},
    {0x69E2, "creature", "bwd c jump out", "JA_SE_CM_BWD_C_JUMP_OUT"},
    {0x69E3, "creature", "bwd c in sand", "JA_SE_CM_BWD_C_IN_SAND"},
    {0x69E5, "object", "boss warp appear", "JA_SE_OBJ_BOSS_WARP_APPEAR"},
    {0x69E9, "object", "lupy out", "JA_SE_OBJ_LUPY_OUT"},
    {0x69ED, "object", "item out", "JA_SE_OBJ_ITEM_OUT"},
    {0x69F1, "object", "bokkuri pick up", "JA_SE_OBJ_BOKKURI_PICK_UP"},
    {0x69F2, "object", "bokkuri bound", "JA_SE_OBJ_BOKKURI_BOUND"},
    {0x6A0E, "object", "kokiri h crash", "JA_SE_OBJ_KOKIRI_H_CRASH"},
    {0x6A20, "object", "gn sw dr light", "JA_SE_OBJ_GN_SW_DR_LIGHT"},
    {0x6A21, "object", "gn sw dr break", "JA_SE_OBJ_GN_SW_DR_BREAK"},
    {0x6A36, "object", "pg eball exp l", "JA_SE_OBJ_PG_EBALL_EXP_L"},
    {0x701D, "object", "wind tag", "JA_SE_OBJ_WIND_TAG"},
    {0x7025, "object", "warp eff sus", "JA_SE_OBJ_WARP_EFF_SUS"},
    {0x7028, "object", "mirror light", "JA_SE_OBJ_MIRROR_LIGHT"},
    {0x7032, "creature", "fm spot", "JA_SE_CM_FM_SPOT"},
    {0x704A, "object", "boss warp lv", "JA_SE_OBJ_BOSS_WARP_LV"},
    {0x7050, "object", "atk vine mp suck", "JA_SE_OBJ_ATK_VINE_MP_SUCK"},
    {0x7059, "object", "boss tf warp sus", "JA_SE_OBJ_BOSS_TF_WARP_SUS"},
    {0x783F, "creature", "drg mtop magma", "JA_SE_CM_DRG_MTOP_MAGMA"},
    {0x7840, "object", "togetoge out", "JA_SE_OBJ_TOGETOGE_OUT"},
    {0x7841, "object", "togetoge move", "JA_SE_OBJ_TOGETOGE_MOVE"},
    {0x7842, "object", "togetoge in", "JA_SE_OBJ_TOGETOGE_IN"},
};
const NamedId kParticles[] = {
    {0x000C, "everywhere", "ng", "ID_AK_JN_NG"},
    {0x0010, "everywhere", "criticalhitflash", "ID_AK_JN_CRITICALHITFLASH"},
    {0x0013, "everywhere", "siboubakuen", "ID_AK_JN_SIBOUBAKUEN"},
    {0x0014, "everywhere", "siboupofu", "ID_AK_JN_SIBOUPOFU"},
    {0x0015, "everywhere", "sibouspirit", "ID_AK_JN_SIBOUSPIRIT"},
    {0x0016, "everywhere", "sibouflash", "ID_AK_JN_SIBOUFLASH"},
    {0x003D, "everywhere", "wp hamon01", "ID_IT_JN_WP_HAMON01"},
    {0x003F, "everywhere", "wp hamon03", "ID_IT_JN_WP_HAMON03"},
    {0x0055, "everywhere", "arwice out00", "ID_IT_JN_ARWICE_OUT00"},
    {0x01EA, "everywhere", "torch", "ID_AK_JN_TORCH"},
    {0x027B, "everywhere", "piyohit00", "ID_IT_JN_PIYOHIT00"},
    {0x029E, "everywhere", "arwi hita00", "ID_IT_JN_ARWI_HITA00"},
    {0x043C, "everywhere", "puchi shiboua", "ID_IT_JN_PUCHI_SHIBOUA"},
    {0x043D, "everywhere", "puchi shiboub", "ID_IT_JN_PUCHI_SHIBOUB"},
    {0x043E, "everywhere", "puchi shibouc", "ID_IT_JN_PUCHI_SHIBOUC"},
    {0x4004, "everywhere", "o kagerou00", "ID_AK_JP_O_KAGEROU00"},
    {0x810F, "stage", "rbridge flsh00", "ID_IT_SN_RBRIDGE_FLSH00"},
    {0x8119, "stage", "rbridge app00", "ID_IT_SN_RBRIDGE_APP00"},
    {0x8120, "stage", "heatfloor00", "ID_AK_SN_HEATFLOOR00"},
    {0x8127, "stage", "amothflash00", "ID_AK_SN_AMOTHFLASH00"},
    {0x8128, "stage", "amothhahen00", "ID_AK_SN_AMOTHHAHEN00"},
    {0x813E, "stage", "dk block s00", "ID_IT_SN_DK_BLOCK_S00"},
    {0x813F, "stage", "dk block l00", "ID_IT_SN_DK_BLOCK_L00"},
    {0x8141, "stage", "dk pole s00", "ID_IT_SN_DK_POLE_S00"},
    {0x8142, "stage", "dk pole l00", "ID_IT_SN_DK_POLE_L00"},
    {0x8143, "stage", "dk bridge l00", "ID_IT_SN_DK_BRIDGE_L00"},
    {0x8144, "stage", "dk bridge s00", "ID_IT_SN_DK_BRIDGE_S00"},
    {0x8145, "stage", "dk bridge tenita00", "ID_IT_SN_DK_BRIDGE_TENITA00"},
    {0x814C, "stage", "heatfloor01", "ID_AK_SN_HEATFLOOR01"},
    {0x8156, "stage", "amothbombmouth", "ID_AK_SN_AMOTHBOMBMOUTH"},
    {0x8157, "stage", "amothbombeye", "ID_AK_SN_AMOTHBOMBEYE"},
    {0x816C, "stage", "tgsyoku rock00", "ID_IT_SN_TGSYOKU_ROCK00"},
    {0x81C8, "stage", "bstwarp00", "ID_AK_SN_BSTWARP00"},
    {0x820A, "stage", "bosswarpwind00 820a", "ID_AK_SN_BOSSWARPWIND00_820A"},
    {0x820B, "stage", "bosswarpwind01 820b", "ID_AK_SN_BOSSWARPWIND01_820B"},
    {0x820C, "stage", "bosswarpkirakira00 820c", "ID_AK_SN_BOSSWARPKIRAKIRA00_820C"},
    {0x821E, "stage", "bpgdarkshot00", "ID_AK_SN_BPGDARKSHOT00"},
    {0x8244, "stage", "bpgsmashdarkshot00", "ID_AK_SN_BPGSMASHDARKSHOT00"},
    {0x8245, "stage", "bpghitdarkshot00", "ID_AK_SN_BPGHITDARKSHOT00"},
    {0x8246, "stage", "bpghitdarkshot01", "ID_AK_SN_BPGHITDARKSHOT01"},
    {0x82A4, "stage", "kokirihousesplash00", "ID_AK_SN_KOKIRIHOUSESPLASH00"},
    {0x82A5, "stage", "kokirihousesplash01", "ID_AK_SN_KOKIRIHOUSESPLASH01"},
    {0x82A6, "stage", "kokirihousehamon00", "ID_AK_SN_KOKIRIHOUSEHAMON00"},
    {0x82A7, "stage", "kokirihousehamon01", "ID_AK_SN_KOKIRIHOUSEHAMON01"},
    {0x82B0, "stage", "takiura hamon00", "ID_IT_SN_TAKIURA_HAMON00"},
    {0x841D, "stage", "shindentf light00", "ID_IT_SN_SHINDENTF_LIGHT00"},
    {0x841E, "stage", "shindentf star00", "ID_IT_SN_SHINDENTF_STAR00"},
    {0x8423, "stage", "demo29 star00", "ID_IT_SN_DEMO29_STAR00"},
    {0xA125, "stage", "amothsmoke00", "ID_AK_ST_AMOTHSMOKE00"},
    {0xA126, "stage", "amothsmoke01", "ID_AK_ST_AMOTHSMOKE01"},
    {0xA13D, "stage", "dk block smoke00", "ID_IT_ST_DK_BLOCK_SMOKE00"},
    {0xA140, "stage", "dk pole smoke00", "ID_IT_ST_DK_POLE_SMOKE00"},
    {0xA146, "stage", "dk bridge smoke00", "ID_IT_ST_DK_BRIDGE_SMOKE00"},
    {0xA154, "stage", "amothsmoke02", "ID_AK_ST_AMOTHSMOKE02"},
    {0xA155, "stage", "amothsmoke03", "ID_AK_ST_AMOTHSMOKE03"},
    {0xA16D, "stage", "tgsyoku smoke00", "ID_IT_ST_TGSYOKU_SMOKE00"},
    {0xA328, "stage", "otomosmoke00", "ID_AK_ST_OTOMOSMOKE00"},
    {0xA33B, "stage", "otomosmoke01", "ID_AK_ST_OTOMOSMOKE01"},
};
// Characters demos often use, by their stage actor names (the names the game's event and demo data
// give them); our own words. The NPC catalogue's residents and the game objects come on top (editor).
const struct {
    const char *actor, *label, *archive, *id;
} kActors[] = {
    {"Dk", "Helmaroc King (cutscene)", "Dk", "helmaroc"},
    {"kamome", "Seagull", "Kamome", "gull"},
    {"Ls1", "Aryll", "", "aryll"},
    {"Zl1", "Tetra", "", "tetra"},
    {"Ob1", "Grandma", "", "grandma"},
    {"Md1", "Medli", "", "medli"},
    {"Cb1", "Makar", "", "makar"},
    {"Ship", "King of Red Lions (the boat)", "Ship", "boat"},
    {"Kb", "Pig", "Kb", "pig"},
    {"Bk", "Bokoblin", "Bk", "bokoblin"},
    {"Mo2", "Moblin", "Mo2", "moblin"},
};
std::string label_of(std::span<const NamedId> list, uint32_t id) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%04X", unsigned(id));
    for (const auto& n : list)
        if (n.id == id) return std::string(n.group) + ": " + n.name + " (" + hex + ")";
    return hex;
}
}  // namespace
std::span<const NamedId> sound_names() { return kSounds; }
std::span<const NamedId> particle_names() { return kParticles; }
std::string sound_label(uint32_t id) { return label_of(kSounds, id); }
std::string particle_label(uint32_t id) { return label_of(kParticles, id); }

const std::vector<DemoActorName>& demo_actor_names() {
    static const std::vector<DemoActorName> names = [] {
        std::vector<DemoActorName> v;
        for (const auto& a : kActors) v.push_back({a.actor, a.label, a.archive, a.id, "characters"});
        return v;
    }();
    return names;
}

std::span<const char* const> link_mode_names() {
    static const std::vector<const char*> names = [] {
        std::vector<const char*> v;
        for (const auto& [n, value] : demo_scene::kLinkModes) v.push_back(n);
        return v;
    }();
    return names;
}

std::string suggest_demo_id(const std::string& base, const std::vector<std::string>& taken) {
    std::string id;
    for (char c : base) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) id += char(std::tolower(u));
        else if (!id.empty() && id.back() != '-') id += '-';
        if (id.size() >= 18) break;
    }
    while (!id.empty() && id.back() == '-') id.pop_back();
    if (id.empty()) id = "actor";
    auto used = [&](const std::string& s) { return std::find(taken.begin(), taken.end(), s) != taken.end(); };
    if (!used(id)) return id;
    for (int n = 2; n < 1000; n++) {
        const std::string next = id + "-" + std::to_string(n);
        if (!used(next)) return next;
    }
    return id;
}

bool matches_query(const std::string& text, const std::string& query) {
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string t = lower(text), q = lower(query);
    size_t at = 0;
    while (at < q.size()) {
        while (at < q.size() && q[at] == ' ') at++;
        size_t end = q.find(' ', at);
        if (end == std::string::npos) end = q.size();
        if (end > at && t.find(q.substr(at, end - at)) == std::string::npos) return false;
        at = end;
    }
    return true;
}
}  // namespace editor
