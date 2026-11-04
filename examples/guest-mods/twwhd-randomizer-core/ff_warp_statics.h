/* Complete public CC0 __sinit_hd_screen_sea_chart_menu_cpp0267A29C,
 * hd_screen_sea_chart_menu.cpp a874e1a9. MIT misc_rando_features inserts the
 * FF coordinate bytes at the native pair8.x/pair8.y seam. The declarative
 * name rows enumerate every public SafeString initializer in source order. */
#pragma once
#ifndef R_FFW
#define R_FFW(t, a) (*(volatile t *)(a))
#endif
WWHD_GAME_FUNC(0x028F026C, void, rfws_register, (u32 record));
WWHD_GAME_FUNC(0x028ED6F8, void, rfws_first, (u32 object));
WWHD_GAME_FUNC(0x028EAB2C, void, rfws_second, (u32 object));
WWHD_REPLACE(0x0267A29C, void, randomizer_ff_warp_statics, (void)) {
  const u32 words = (u32)&WWHD_GAME_DATA(0x10495400, u8);
  R_FFW(u32, words + 12) = 0;
  R_FFW(u32, words + 8) = 0;
  R_FFW(u32, words + 4) = 0;
  R_FFW(u32, words) = 0;
  rfws_register((u32)&WWHD_GAME_DATA(0x101F61DC, u8));
  const unsigned int minimum = WWHD_GAME_DATA(0x100F7ED4, u32),
                     maximum = WWHD_GAME_DATA(0x100F7ED8, u32);
  WWHD_GAME_DATA(0x104953E4, u32) = minimum;
  WWHD_GAME_DATA(0x104953E8, u32) = maximum;
  rfws_first((u32)&WWHD_GAME_DATA(0x104953FC, u8));
  rfws_register((u32)&WWHD_GAME_DATA(0x101F61E8, u8));
  rfws_second((u32)&WWHD_GAME_DATA(0x104953FD, u8));
  rfws_register((u32)&WWHD_GAME_DATA(0x101F61F4, u8));
  const unsigned int extent = WWHD_GAME_DATA(0x100F7EDC, u32);
  WWHD_GAME_DATA(0x104953F0, u32) = extent;
  const unsigned int limit = WWHD_GAME_DATA(0x100F7EE0, u32);
  WWHD_GAME_DATA(0x104953F4, u32) = limit;
  WWHD_GAME_DATA(0x104953F8, u32) = limit;
  WWHD_GAME_DATA(0x104953EC, u32) = extent;
  static const struct {
    u16 object, text, count;
  } names[] = {
      {0x000, 0x0C8, 1}, /* sSeaMenuMainName: B_Area_00 */
      {0x008, 0x784, 1}, /* sSeaMenuTextName: L_LinkPos_00 */
      {0x010, 0x020, 1}, /* sSeaMenuTextType: N_Rotate_00 */
      {0x018, 0x794, 1}, /* sSeaMenuMarkerPrefix: P_TreasureGet_ */
      {0x04C, 0x704, 1}, /* sSeaMenuPaneNames[0]: N_MapMark_03 */
      {0x054, 0x714, 1}, /* sSeaMenuPaneNames[1]: N_MapMark_12 */
      {0x05C, 0x724, 1}, /* sSeaMenuPaneNames[2]: N_MapMark_17 */
      {0x064, 0x734, 1}, /* sSeaMenuPaneNames[3]: N_MapMark_22 */
      {0x06C, 0x744, 1}, /* sSeaMenuPaneNames[4]: N_MapMark_26 */
      {0x074, 0x754, 1}, /* sSeaMenuPaneNames[5]: N_MapMark_31 */
      {0x07C, 0x764, 1}, /* sSeaMenuPaneNames[6]: N_MapMark_40 */
      {0x084, 0x774, 1}, /* sSeaMenuPaneNames[7]: N_MapMark_44 */
      {0x08C, 0xC0C, 1}, /* sSeaMenuInfoNames[0]: L_BtnMapBalloon_00 */
      {0x094, 0x010, 1}, /* sSeaMenuInfoNames[1]: L_MapAreaNow_00 */
      {0x09C, 0x784, 1}, /* sSeaMenuInfoNames[2]: L_LinkPos_00 */
      {0x0A4, 0xC20, 1}, /* sSeaMenuAuxiliaryNames[0]: BtnMapBalloon_00 */
      {0x0AC, 0x6F4, 1}, /* sSeaMenuAuxiliaryNames[1]: MapAreaNow_00 */
      {0x0B4, 0xC34, 1}, /* sSeaMenuAuxiliaryNames[2]: LinkPos_00 */
      {0x0D0, 0x7A4, 1}, /* sSeaMenuAnimationGroups[0]: G_All_00 */
      {0x0D8, 0x0D4, 1}, /* sSeaMenuAnimationGroups[1]: G_MapTouch_00 */
      {0x0E0, 0x0E4, 1}, /* sSeaMenuAnimationGroups[2]: G_MapTouch_01 */
      {0x0E8, 0x0F4, 1}, /* sSeaMenuAnimationGroups[3]: G_MapTouch_02 */
      {0x0F0, 0x104, 1}, /* sSeaMenuAnimationGroups[4]: G_MapTouch_03 */
      {0x0F8, 0x114, 1}, /* sSeaMenuAnimationGroups[5]: G_MapTouch_04 */
      {0x100, 0x124, 1}, /* sSeaMenuAnimationGroups[6]: G_MapTouch_05 */
      {0x108, 0x134, 1}, /* sSeaMenuAnimationGroups[7]: G_MapTouch_06 */
      {0x110, 0x144, 1}, /* sSeaMenuAnimationGroups[8]: G_MapTouch_07 */
      {0x118, 0x154, 1}, /* sSeaMenuAnimationGroups[9]: G_MapTouch_08 */
      {0x120, 0x164, 1}, /* sSeaMenuAnimationGroups[10]: G_MapTouch_09 */
      {0x128, 0x174, 1}, /* sSeaMenuAnimationGroups[11]: G_MapTouch_10 */
      {0x130, 0x184, 1}, /* sSeaMenuAnimationGroups[12]: G_MapTouch_11 */
      {0x138, 0x194, 1}, /* sSeaMenuAnimationGroups[13]: G_MapTouch_12 */
      {0x140, 0x1A4, 1}, /* sSeaMenuAnimationGroups[14]: G_MapTouch_13 */
      {0x148, 0x1B4, 1}, /* sSeaMenuAnimationGroups[15]: G_MapTouch_14 */
      {0x150, 0x1C4, 1}, /* sSeaMenuAnimationGroups[16]: G_MapTouch_15 */
      {0x158, 0x1D4, 1}, /* sSeaMenuAnimationGroups[17]: G_MapTouch_16 */
      {0x160, 0x1E4, 1}, /* sSeaMenuAnimationGroups[18]: G_MapTouch_17 */
      {0x168, 0x1F4, 1}, /* sSeaMenuAnimationGroups[19]: G_MapTouch_18 */
      {0x170, 0x204, 1}, /* sSeaMenuAnimationGroups[20]: G_MapTouch_19 */
      {0x178, 0x214, 1}, /* sSeaMenuAnimationGroups[21]: G_MapTouch_20 */
      {0x180, 0x224, 1}, /* sSeaMenuAnimationGroups[22]: G_MapTouch_21 */
      {0x188, 0x234, 1}, /* sSeaMenuAnimationGroups[23]: G_MapTouch_22 */
      {0x190, 0x244, 1}, /* sSeaMenuAnimationGroups[24]: G_MapTouch_23 */
      {0x198, 0x254, 1}, /* sSeaMenuAnimationGroups[25]: G_MapTouch_24 */
      {0x1A0, 0x264, 1}, /* sSeaMenuAnimationGroups[26]: G_MapTouch_25 */
      {0x1A8, 0x274, 1}, /* sSeaMenuAnimationGroups[27]: G_MapTouch_26 */
      {0x1B0, 0x284, 1}, /* sSeaMenuAnimationGroups[28]: G_MapTouch_27 */
      {0x1B8, 0x294, 1}, /* sSeaMenuAnimationGroups[29]: G_MapTouch_28 */
      {0x1C0, 0x2A4, 1}, /* sSeaMenuAnimationGroups[30]: G_MapTouch_29 */
      {0x1C8, 0x2B4, 1}, /* sSeaMenuAnimationGroups[31]: G_MapTouch_30 */
      {0x1D0, 0x2C4, 1}, /* sSeaMenuAnimationGroups[32]: G_MapTouch_31 */
      {0x1D8, 0x2D4, 1}, /* sSeaMenuAnimationGroups[33]: G_MapTouch_32 */
      {0x1E0, 0x2E4, 1}, /* sSeaMenuAnimationGroups[34]: G_MapTouch_33 */
      {0x1E8, 0x2F4, 1}, /* sSeaMenuAnimationGroups[35]: G_MapTouch_34 */
      {0x1F0, 0x304, 1}, /* sSeaMenuAnimationGroups[36]: G_MapTouch_35 */
      {0x1F8, 0x314, 1}, /* sSeaMenuAnimationGroups[37]: G_MapTouch_36 */
      {0x200, 0x324, 1}, /* sSeaMenuAnimationGroups[38]: G_MapTouch_37 */
      {0x208, 0x334, 1}, /* sSeaMenuAnimationGroups[39]: G_MapTouch_38 */
      {0x210, 0x344, 1}, /* sSeaMenuAnimationGroups[40]: G_MapTouch_39 */
      {0x218, 0x354, 1}, /* sSeaMenuAnimationGroups[41]: G_MapTouch_40 */
      {0x220, 0x364, 1}, /* sSeaMenuAnimationGroups[42]: G_MapTouch_41 */
      {0x228, 0x374, 1}, /* sSeaMenuAnimationGroups[43]: G_MapTouch_42 */
      {0x230, 0x384, 1}, /* sSeaMenuAnimationGroups[44]: G_MapTouch_43 */
      {0x238, 0x394, 1}, /* sSeaMenuAnimationGroups[45]: G_MapTouch_44 */
      {0x240, 0x3A4, 1}, /* sSeaMenuAnimationGroups[46]: G_MapTouch_45 */
      {0x248, 0x3B4, 1}, /* sSeaMenuAnimationGroups[47]: G_MapTouch_46 */
      {0x250, 0x3C4, 1}, /* sSeaMenuAnimationGroups[48]: G_MapTouch_47 */
      {0x258, 0x3D4, 1}, /* sSeaMenuAnimationGroups[49]: G_MapTouch_48 */
      {0x260, 0x7B0, 1}, /* sSeaMenuAnimationGroups[50]: G_TreasureGet_00 */
      {0x268, 0x7C4, 1}, /* sSeaMenuAnimationGroups[51]: G_TreasureGet_01 */
      {0x270, 0x7D8, 1}, /* sSeaMenuAnimationGroups[52]: G_TreasureGet_02 */
      {0x278, 0x7EC, 1}, /* sSeaMenuAnimationGroups[53]: G_TreasureGet_03 */
      {0x280, 0x800, 1}, /* sSeaMenuAnimationGroups[54]: G_TreasureGet_04 */
      {0x288, 0x814, 1}, /* sSeaMenuAnimationGroups[55]: G_TreasureGet_05 */
      {0x290, 0x828, 1}, /* sSeaMenuAnimationGroups[56]: G_TreasureGet_06 */
      {0x298, 0x83C, 1}, /* sSeaMenuAnimationGroups[57]: G_TreasureGet_07 */
      {0x2A0, 0x850, 1}, /* sSeaMenuAnimationGroups[58]: G_TreasureGet_08 */
      {0x2A8, 0x864, 1}, /* sSeaMenuAnimationGroups[59]: G_TreasureGet_09 */
      {0x2B0, 0x878, 1}, /* sSeaMenuAnimationGroups[60]: G_TreasureGet_10 */
      {0x2B8, 0x88C, 1}, /* sSeaMenuAnimationGroups[61]: G_TreasureGet_11 */
      {0x2C0, 0x8A0, 1}, /* sSeaMenuAnimationGroups[62]: G_TreasureGet_12 */
      {0x2C8, 0x8B4, 1}, /* sSeaMenuAnimationGroups[63]: G_TreasureGet_13 */
      {0x2D0, 0x8C8, 1}, /* sSeaMenuAnimationGroups[64]: G_TreasureGet_14 */
      {0x2D8, 0x8DC, 1}, /* sSeaMenuAnimationGroups[65]: G_TreasureGet_15 */
      {0x2E0, 0x8F0, 1}, /* sSeaMenuAnimationGroups[66]: G_TreasureGet_16 */
      {0x2E8, 0x904, 1}, /* sSeaMenuAnimationGroups[67]: G_TreasureGet_17 */
      {0x2F0, 0x918, 1}, /* sSeaMenuAnimationGroups[68]: G_TreasureGet_18 */
      {0x2F8, 0x92C, 1}, /* sSeaMenuAnimationGroups[69]: G_TreasureGet_19 */
      {0x300, 0x940, 1}, /* sSeaMenuAnimationGroups[70]: G_TreasureGet_20 */
      {0x308, 0x954, 1}, /* sSeaMenuAnimationGroups[71]: G_TreasureGet_21 */
      {0x310, 0x968, 1}, /* sSeaMenuAnimationGroups[72]: G_TreasureGet_22 */
      {0x318, 0x97C, 1}, /* sSeaMenuAnimationGroups[73]: G_TreasureGet_23 */
      {0x320, 0x990, 1}, /* sSeaMenuAnimationGroups[74]: G_TreasureGet_24 */
      {0x328, 0x9A4, 1}, /* sSeaMenuAnimationGroups[75]: G_TreasureGet_25 */
      {0x330, 0x9B8, 1}, /* sSeaMenuAnimationGroups[76]: G_TreasureGet_26 */
      {0x338, 0x9CC, 1}, /* sSeaMenuAnimationGroups[77]: G_TreasureGet_27 */
      {0x340, 0x9E0, 1}, /* sSeaMenuAnimationGroups[78]: G_TreasureGet_28 */
      {0x348, 0x9F4, 1}, /* sSeaMenuAnimationGroups[79]: G_TreasureGet_29 */
      {0x350, 0xA08, 1}, /* sSeaMenuAnimationGroups[80]: G_TreasureGet_30 */
      {0x358, 0xA1C, 1}, /* sSeaMenuAnimationGroups[81]: G_TreasureGet_31 */
      {0x360, 0xA30, 1}, /* sSeaMenuAnimationGroups[82]: G_TreasureGet_32 */
      {0x368, 0xA44, 1}, /* sSeaMenuAnimationGroups[83]: G_TreasureGet_33 */
      {0x370, 0xA58, 1}, /* sSeaMenuAnimationGroups[84]: G_TreasureGet_34 */
      {0x378, 0xA6C, 1}, /* sSeaMenuAnimationGroups[85]: G_TreasureGet_35 */
      {0x380, 0xA80, 1}, /* sSeaMenuAnimationGroups[86]: G_TreasureGet_36 */
      {0x388, 0xA94, 1}, /* sSeaMenuAnimationGroups[87]: G_TreasureGet_37 */
      {0x390, 0xAA8, 1}, /* sSeaMenuAnimationGroups[88]: G_TreasureGet_38 */
      {0x398, 0xABC, 1}, /* sSeaMenuAnimationGroups[89]: G_TreasureGet_39 */
      {0x3A0, 0xAD0, 1}, /* sSeaMenuAnimationGroups[90]: G_TreasureGet_40 */
      {0x3A8, 0xAE4, 1}, /* sSeaMenuAnimationGroups[91]: G_TreasureGet_41 */
      {0x3B0, 0xAF8, 1}, /* sSeaMenuAnimationGroups[92]: G_TreasureGet_42 */
      {0x3B8, 0xB0C, 1}, /* sSeaMenuAnimationGroups[93]: G_TreasureGet_43 */
      {0x3C0, 0xB20, 1}, /* sSeaMenuAnimationGroups[94]: G_TreasureGet_44 */
      {0x3C8, 0xB34, 1}, /* sSeaMenuAnimationGroups[95]: G_TreasureGet_45 */
      {0x3D0, 0xB48, 1}, /* sSeaMenuAnimationGroups[96]: G_TreasureGet_46 */
      {0x3D8, 0xB5C, 1}, /* sSeaMenuAnimationGroups[97]: G_TreasureGet_47 */
      {0x3E0, 0xB70, 1}, /* sSeaMenuAnimationGroups[98]: G_TreasureGet_48 */
      {0x3E8, 0xB84, 1}, /* sSeaMenuAnimationGroups[99]: G_Korog_00 */
      {0x3F0, 0xB90, 1}, /* sSeaMenuAnimationGroups[100]: G_Korog_01 */
      {0x3F8, 0xB9C, 1}, /* sSeaMenuAnimationGroups[101]: G_Korog_02 */
      {0x400, 0xBA8, 1}, /* sSeaMenuAnimationGroups[102]: G_Korog_03 */
      {0x408, 0xBB4, 1}, /* sSeaMenuAnimationGroups[103]: G_Korog_04 */
      {0x410, 0xBC0, 1}, /* sSeaMenuAnimationGroups[104]: G_Korog_05 */
      {0x418, 0xBCC, 1}, /* sSeaMenuAnimationGroups[105]: G_Korog_06 */
      {0x420, 0xBD8, 1}, /* sSeaMenuAnimationGroups[106]: G_Korog_07 */
      {0x428, 0xBE4, 1}, /* sSeaMenuAnimationGroups[107]: G_MapMark_00 */
      {0x430, 0x3E4, 1}, /* sSeaMenuAnimationGroups[108]: G_OpenDemo_00 */
      {0x438, 0x3F4, 1}, /* sSeaMenuAnimationGroups[109]: G_OpenDemo_01 */
      {0x440, 0x404, 1}, /* sSeaMenuAnimationGroups[110]: G_OpenDemo_02 */
      {0x448, 0x414, 1}, /* sSeaMenuAnimationGroups[111]: G_OpenDemo_03 */
      {0x450, 0x424, 1}, /* sSeaMenuAnimationGroups[112]: G_OpenDemo_04 */
      {0x458, 0x434, 1}, /* sSeaMenuAnimationGroups[113]: G_OpenDemo_05 */
      {0x460, 0x444, 1}, /* sSeaMenuAnimationGroups[114]: G_OpenDemo_06 */
      {0x468, 0x454, 1}, /* sSeaMenuAnimationGroups[115]: G_OpenDemo_07 */
      {0x470, 0x464, 1}, /* sSeaMenuAnimationGroups[116]: G_OpenDemo_08 */
      {0x478, 0x474, 1}, /* sSeaMenuAnimationGroups[117]: G_OpenDemo_09 */
      {0x480, 0x484, 1}, /* sSeaMenuAnimationGroups[118]: G_OpenDemo_10 */
      {0x488, 0x494, 1}, /* sSeaMenuAnimationGroups[119]: G_OpenDemo_11 */
      {0x490, 0x4A4, 1}, /* sSeaMenuAnimationGroups[120]: G_OpenDemo_12 */
      {0x498, 0x4B4, 1}, /* sSeaMenuAnimationGroups[121]: G_OpenDemo_13 */
      {0x4A0, 0x4C4, 1}, /* sSeaMenuAnimationGroups[122]: G_OpenDemo_14 */
      {0x4A8, 0x4D4, 1}, /* sSeaMenuAnimationGroups[123]: G_OpenDemo_15 */
      {0x4B0, 0x4E4, 1}, /* sSeaMenuAnimationGroups[124]: G_OpenDemo_16 */
      {0x4B8, 0x4F4, 1}, /* sSeaMenuAnimationGroups[125]: G_OpenDemo_17 */
      {0x4C0, 0x504, 1}, /* sSeaMenuAnimationGroups[126]: G_OpenDemo_18 */
      {0x4C8, 0x514, 1}, /* sSeaMenuAnimationGroups[127]: G_OpenDemo_19 */
      {0x4D0, 0x524, 1}, /* sSeaMenuAnimationGroups[128]: G_OpenDemo_20 */
      {0x4D8, 0x534, 1}, /* sSeaMenuAnimationGroups[129]: G_OpenDemo_21 */
      {0x4E0, 0x544, 1}, /* sSeaMenuAnimationGroups[130]: G_OpenDemo_22 */
      {0x4E8, 0x554, 1}, /* sSeaMenuAnimationGroups[131]: G_OpenDemo_23 */
      {0x4F0, 0x564, 1}, /* sSeaMenuAnimationGroups[132]: G_OpenDemo_24 */
      {0x4F8, 0x574, 1}, /* sSeaMenuAnimationGroups[133]: G_OpenDemo_25 */
      {0x500, 0x584, 1}, /* sSeaMenuAnimationGroups[134]: G_OpenDemo_26 */
      {0x508, 0x594, 1}, /* sSeaMenuAnimationGroups[135]: G_OpenDemo_27 */
      {0x510, 0x5A4, 1}, /* sSeaMenuAnimationGroups[136]: G_OpenDemo_28 */
      {0x518, 0x5B4, 1}, /* sSeaMenuAnimationGroups[137]: G_OpenDemo_29 */
      {0x520, 0x5C4, 1}, /* sSeaMenuAnimationGroups[138]: G_OpenDemo_30 */
      {0x528, 0x5D4, 1}, /* sSeaMenuAnimationGroups[139]: G_OpenDemo_31 */
      {0x530, 0x5E4, 1}, /* sSeaMenuAnimationGroups[140]: G_OpenDemo_32 */
      {0x538, 0x5F4, 1}, /* sSeaMenuAnimationGroups[141]: G_OpenDemo_33 */
      {0x540, 0x604, 1}, /* sSeaMenuAnimationGroups[142]: G_OpenDemo_34 */
      {0x548, 0x614, 1}, /* sSeaMenuAnimationGroups[143]: G_OpenDemo_35 */
      {0x550, 0x624, 1}, /* sSeaMenuAnimationGroups[144]: G_OpenDemo_36 */
      {0x558, 0x634, 1}, /* sSeaMenuAnimationGroups[145]: G_OpenDemo_37 */
      {0x560, 0x644, 1}, /* sSeaMenuAnimationGroups[146]: G_OpenDemo_38 */
      {0x568, 0x654, 1}, /* sSeaMenuAnimationGroups[147]: G_OpenDemo_39 */
      {0x570, 0x664, 1}, /* sSeaMenuAnimationGroups[148]: G_OpenDemo_40 */
      {0x578, 0x674, 1}, /* sSeaMenuAnimationGroups[149]: G_OpenDemo_41 */
      {0x580, 0x684, 1}, /* sSeaMenuAnimationGroups[150]: G_OpenDemo_42 */
      {0x588, 0x694, 1}, /* sSeaMenuAnimationGroups[151]: G_OpenDemo_43 */
      {0x590, 0x6A4, 1}, /* sSeaMenuAnimationGroups[152]: G_OpenDemo_44 */
      {0x598, 0x6B4, 1}, /* sSeaMenuAnimationGroups[153]: G_OpenDemo_45 */
      {0x5A0, 0x6C4, 1}, /* sSeaMenuAnimationGroups[154]: G_OpenDemo_46 */
      {0x5A8, 0x6D4, 1}, /* sSeaMenuAnimationGroups[155]: G_OpenDemo_47 */
      {0x5B0, 0x6E4, 1}, /* sSeaMenuAnimationGroups[156]: G_OpenDemo_48 */
      {0x5B8, 0x02C, 1}, /* sSeaMenuAnimationGroups[157]: G_Island_00 */
      {0x5C0, 0x038, 1}, /* sSeaMenuAnimationGroups[158]: G_Island_03 */
      {0x5C8, 0x044, 1}, /* sSeaMenuAnimationGroups[159]: G_Island_10 */
      {0x5D0, 0x050, 1}, /* sSeaMenuAnimationGroups[160]: G_Island_12 */
      {0x5D8, 0x05C, 1}, /* sSeaMenuAnimationGroups[161]: G_Island_19 */
      {0x5E0, 0x068, 1}, /* sSeaMenuAnimationGroups[162]: G_Island_22 */
      {0x5E8, 0x074, 1}, /* sSeaMenuAnimationGroups[163]: G_Island_25 */
      {0x5F0, 0x080, 1}, /* sSeaMenuAnimationGroups[164]: G_Island_39 */
      {0x5F8, 0x08C, 1}, /* sSeaMenuAnimationGroups[165]: G_Island_40 */
      {0x600, 0x098, 1}, /* sSeaMenuAnimationGroups[166]: G_Island_43 */
      {0x608, 0x0A4, 1}, /* sSeaMenuAnimationGroups[167]: G_Island_44 */
      {0x610, 0xBF4,
       49}, /* sSeaMenuAnimationNames[0]..sSeaMenuAnimationNames[1]: MapTouch */
      {0x798, 0x0B0, 49}, /* sSeaMenuAnimationNames[49]..sSeaMenuAnimationNames[50]:
                             TreasureGet */
      {0x920, 0x000,
       8}, /* sSeaMenuAnimationNames[98]..sSeaMenuAnimationNames[99]: Korog */
      {0x960, 0x0BC, 1}, /* sSeaMenuAnimationNames[106]: MapMarkLoop */
      {0x968, 0xC00, 60}, /* sSeaMenuAnimationNames[107]..sSeaMenuAnimationNames[108]:
                             OpenDemo */
      {0xB48, 0x008, 1}, /* sSeaMenuLayoutName: Map_00 */
  };
  const u32 objects = (u32)&WWHD_GAME_DATA(0x104953C4, u8),
            strings = (u32)&WWHD_GAME_DATA(0x100F7EE4, u8);
  const u32 vtable = (u32)&WWHD_GAME_DATA(0x100F7B4C, u8);
  for (u32 row = 0; row < sizeof(names) / sizeof(names[0]); row++)
    for (u32 index = 0; index < names[row].count; index++) {
      const u32 object = objects + names[row].object + index * 8;
      R_FFW(u32, object + 4) = vtable;
      R_FFW(u32, object) = strings + names[row].text;
    }
  static const u8 coordinates[16] = {254, 254, 0, 254, 2,   254, 254, 0,
                                     1,   0,   2, 2,   254, 3,   255, 255};
  const u32 coords = (u32)&WWHD_GAME_DATA(0x10495480, u8);
  for (u32 index = 0; index < 16; index++)
    R_FFW(u8, coords + index) = coordinates[index];
  R_FFW(u8, coords + 16) = 0;
  R_FFW(u8, coords + 18) = 253;
  R_FFW(u8, coords + 19) = 253;
  R_FFW(u8, coords + 17) = 2;
}
