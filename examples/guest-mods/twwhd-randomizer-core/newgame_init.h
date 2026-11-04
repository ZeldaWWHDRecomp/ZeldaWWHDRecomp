/* New-game initialization port from TWWHD Randomizer 1.2.0 custom_funcs.asm.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt.
 * Experimental source port; remaining gameplay patches are not implemented here. */
#pragma once
#include "wwhd/save_flags.h"
#include "seed_init_data.h"
#include "item_grants.h"
#include "chart_reveal.h"

static void randomizer_dungeon_bit(u32 stage,u32 bit) {
    volatile u8* save=randomizer_save();
    const u32 memory=save[0x7BC]==stage ? (u32)save+0x798 : (u32)save+0x3A0+stage*0x24;
    wwhd_dSv_memBit_c_onDungeonItem(memory,bit);
}

static void initialize_randomizer_save(void) {
    volatile u8* save=randomizer_save();
    wwhd_item_func_wind_tact();
    wwhd_item_func_tact_song1();
    randomizer_grant_item(0x78);
    if(init_swordless) {
        wwhd_save_event_set(0x2C02,1);
        wwhd_save_event_set(0x3B08,1);
    }
    wwhd_item_func_pirates_omamori();
    save[0x89]=save[0x8A]=save[0x8F]=save[0x90]=30;
    save[0x33]=save[0x34]=(u8)init_starting_magic;
    *(volatile u16*)(save+0x20)=(u16)init_quarter_hearts;
    *(volatile u16*)(save+0x22)=(u16)(init_quarter_hearts&~3u);
    for(u32 i=0;i<sizeof(init_starting_gear) && init_starting_gear[i]!=255;i++)
        randomizer_grant_item(init_starting_gear[i]);

    wwhd_save_event_write(0xBFFF,init_outset_pig_color);
    for(u32 i=0;i<sizeof(init_story_flags)/sizeof(init_story_flags[0]);i++)
        wwhd_save_event_set(init_story_flags[i],1);
    wwhd_dSv_event_c_onEventBit((u32)save+0x1178,0x0310);

    /* Initial saved stage switches, including the optional opened Dragon Roost pond. */
    *(volatile u32*)(save+0x530)=0x4018028B+(init_open_drc<<8);
    *(volatile u32*)(save+0x534)=0xC0000000;
    *(volatile u32*)(save+0x3A4)=0x02000000;
    *(volatile u32*)(save+0x3A8)=0x80000000;
    *(volatile u32*)(save+0x3AC)=0x41010080;
    *(volatile u32*)(save+0x410)=0x00100200;
    *(volatile u32*)(save+0x414)=2;
    *(volatile u32*)(save+0x418)=0x40;
    *(volatile u32*)(save+0x438)=0x00400000;
    *(volatile u32*)(save+0x45C)=0x2000;
    *(volatile u32*)(save+0x464)=8;
    *(volatile u32*)(save+0x480)=0x400;
    *(volatile u32*)(save+0x4E8)=0x00040040;
    *(volatile u32*)(save+0x4C4)=0xF0042000;
    for(u32 stage=3;stage<=7;stage++) randomizer_dungeon_bit(stage,5);
    save[0xB4]=save[0xB5]=255;
    if(save[0xDE]==255) wwhd_save_event_set(0x3D04,1);
    if(init_skip_rematch_bosses) {
        wwhd_save_event_set(0x3904,1);wwhd_save_event_set(0x3902,1);
        wwhd_save_event_set(0x3901,1);wwhd_save_event_set(0x3A80,1);
    }
    save[0x12F0]=(u8)init_target_type_preference;
    save[0x12F1]=(u8)init_camera_preference;
    save[0x12F2]=(u8)init_first_person_camera_preference;
    save[0x12F5]=(u8)init_gyroscope_preference;
    save[0x12F7]=(u8)init_ui_display_preference;
    randomizer_reveal_starting_chart((u32)save,init_reveal_full_sea_chart);
    wwhd_log("coremod randomizer save defaults applied");
}
