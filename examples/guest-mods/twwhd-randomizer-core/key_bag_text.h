/* Complete public CC0 hdTextUnit_c::tagDispatch026005F8 from
 * hd_text_unit_proc.cpp a874e1a9. MIT misc_rando_features adds only group7
 * commands4B..4F; ordinary groups keep native dispatch and register arguments.
 */
#pragma once
#ifndef R_KEY_TEXT
#define R_KEY_TEXT(type, address) (*(volatile type *)(address))
#endif
WWHD_GAME_FUNC(0x025FB10C, u32, rkt_group0,
               (u32 self, u32 message, u32 index, u32 tag, u32 a7, u32 a8));
WWHD_GAME_FUNC(0x025FB928, u32, rkt_group1,
               (u32 self, u32 message, u32 index, u32 tag, u32 a7, u32 a8));
WWHD_GAME_FUNC(0x026002E4, u32, rkt_group2,
               (u32 self, u32 message, u32 index, u32 tag, u32 a7, u32 a8));
WWHD_GAME_FUNC(0x0260046C, u32, rkt_sound,
               (u32 self, u32 tag, u32 index, u32 original_tag, u32 a7,
                u32 a8));
WWHD_GAME_FUNC(0x02600548, u32, rkt_play_value,
               (u32 self, u32 tag, u32 index, u32 original_tag, u32 a7,
                u32 a8));
WWHD_GAME_FUNC(0x02600598, u32, rkt_play_byte,
               (u32 self, u32 tag, u32 index, u32 original_tag, u32 a7,
                u32 a8));
WWHD_GAME_FUNC(0x025FCB90, u32, rkt_number,
               (u32 self, u32 message, u32 index, u32 count, u32 max_digits,
                u32 no_padding));
WWHD_GAME_FUNC(0x025FCE5C, u32, rkt_unit,
               (u32 self, u32 message, u32 index, u32 label));
WWHD_REPLACE(0x026005F8, s32, randomizer_key_bag_text,
             (u32 self, u32 message, u32 index, u32 tag, u32 a7, u32 a8)) {
  const u16 group = R_KEY_TEXT(u16, tag + 2);
  switch (group) {
  case 0:
    return (s32)rkt_group0(self, message, index, tag, a7, a8);
  case 1:
    return (s32)rkt_group1(self, message, index, tag, a7, a8);
  case 2:
    return (s32)rkt_group2(self, message, index, tag, a7, a8);
  case 4:
    return (s32)rkt_sound(self, tag, index, tag, a7, a8);
  case 5:
    return (s32)rkt_play_value(self, tag, index, tag, a7, a8);
  case 6:
    return (s32)rkt_play_byte(self, tag, index, tag, a7, a8);
  case 7: {
    const u16 command = R_KEY_TEXT(u16, tag + 4);
    if (command < 0x4B || command > 0x4F)
      return -1;
    const u32 stage = command - 0x48;
    const u32 stage_save = WWHD_GAME_DATA(0x101F84DC, u32);
    const u8 current_stage = R_KEY_TEXT(u8, stage_save + 0x7BC);
    const u32 save = WWHD_GAME_DATA(0x101F84DC, u32);
    /* Preserve the selected stage-memory pointer over both native text
     * callbacks. Live dungeon keys are newer than the saved stage array while
     * inside it. */
    const u32 info =
        current_stage == stage ? save + 0x798 : save + 0x3A0 + stage * 0x24;
    const u32 count = R_KEY_TEXT(u8, info + 0x20);
    const u32 number_length = rkt_number(self, message, index, count, 0, 1);
    const u32 next = (unsigned int)(index + number_length);
    /* Big-key ownership is read AFTER number insertion, as in the upstream
     * helper. Native sead::SafeString is exactly {string pointer,vtable
     * pointer}. */
    static const char no_big_key[] = "Unit_Key_00";
    static const char big_key[] = "Unit_Key_01";
    const u32 label[2] = {
        (u32)((R_KEY_TEXT(u8, info + 0x21) & 4) ? big_key : no_big_key),
        (u32)&WWHD_GAME_DATA(0x1010394C, u8)};
    const u32 unit_length = rkt_unit(self, message, next, (u32)label);
    return (s32)(unsigned int)(number_length + unit_length);
  }
  default:
    return -1;
  }
}
