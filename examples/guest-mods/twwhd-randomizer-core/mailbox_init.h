/* CC0 d_a_obj_toripost.cpp createInit; upstream Orca letter uses boss4. */
#pragma once
WWHD_GAME_FUNC(0x025B7D90,u32,randomizer_mail_symbol,(u32 collection,u32 symbol));
WWHD_GAME_FUNC(0x02586ED0,u32,randomizer_mail_delivered,(u32 letter));
WWHD_GAME_FUNC(0x023A3DD0,void,randomizer_mail_animation,(u32 actor,u32 animation,u32 force));
WWHD_GAME_FUNC(0x023A3F08,void,randomizer_mail_matrix,(u32 actor));
WWHD_GAME_FUNC(0x025E55A0,void,randomizer_mail_model_calc,(u32 morph));
WWHD_GAME_FUNC(0x025D674C,void,randomizer_mail_cull,(u32 actor,f32 xmin,f32 ymin,f32 zmin,f32 xmax,f32 ymax,f32 zmax));
WWHD_GAME_FUNC(0x02515F14,void,randomizer_mail_status,(u32 status,u32 weight,u32 flags,u32 actor));
WWHD_GAME_FUNC(0x02516518,void,randomizer_mail_cylinder,(u32 cylinder,u32 source));
WWHD_GAME_FUNC(0x023A3FE8,u32,randomizer_mail_price,(u32 actor));
WWHD_GAME_FUNC(0x023A40E0,void,randomizer_mail_mode,(u32 actor,u32 proc,u32 mode));
WWHD_GAME_FUNC(0x02588BDC,void,randomizer_mail_effect,(u32 position));
WWHD_GAME_FUNC(0x0259F814,void,randomizer_mail_event_actor,(u32 cut,u32 name,u32 actor));
WWHD_REPLACE(0x023A418C,void,randomizer_mailbox_init,(u32 actor)) {
    if(randomizer_mail_symbol(randomizer_daily_save()+0xD4,2))randomizer_daily_stock(0xB503);
    if(wwhd_dComIfGs_checkGetItem(0x31))randomizer_daily_stock(0x7D03);
    if(randomizer_mail_delivered(0xAC03) && randomizer_daily_boss(6))randomizer_daily_stock(0x7C03);
    if(randomizer_daily_boss(4))randomizer_daily_stock(0x7B03);
    const u32 wallet=*(volatile u8*)(randomizer_daily_save()+0x32);
    if(wallet==1 || wallet==2)randomizer_daily_stock(0x7A03);
    *(volatile unsigned int*)(actor+0xA08)=1;
    *(volatile unsigned int*)(actor+0x7C8)=0xFFFFFFFFu;
    *(volatile u8*)(actor+0x38B)=6;*(volatile u8*)(actor+0x389)=5;
    *(volatile unsigned int*)(actor+0x39C)=0x2000000A;
    randomizer_mail_animation(actor,1,0);randomizer_mail_matrix(actor);
    randomizer_mail_model_calc(*(volatile u32*)(actor+0x7EC));
    const u32 morph=*(volatile u32*)(actor+0x7EC),model=*(volatile u32*)(morph+0x90);
    const u32 cull=model?model+0xC8:0;
    const f32 ymax=WWHD_GAME_DATA(0x10031404,f32),zmin=WWHD_GAME_DATA(0x100313F8,f32);
    const f32 zmax=WWHD_GAME_DATA(0x100313FC,f32),ymin=WWHD_GAME_DATA(0x10031400,f32);
    *(volatile u32*)(actor+0x348)=cull;
    randomizer_mail_cull(actor,zmin,ymin,zmin,zmax,ymax,zmax);
    *(volatile unsigned int*)(actor+0x364)=WWHD_GAME_DATA(0x10031408,unsigned int);
    randomizer_mail_status(actor+0x654,255,255,actor);
    randomizer_mail_cylinder(actor+0x690,(u32)&WWHD_GAME_DATA(0x100314C0,u8));
    *(volatile u32*)(actor+0x6D4)=actor+0x654;
    const u32 price=randomizer_mail_price(actor);*(volatile u8*)(actor+0xA00)=(u8)price;
    randomizer_mail_mode(actor,0,0);randomizer_mail_effect(actor+0x314);
    randomizer_mail_event_actor(actor+0x3E0,(u32)&WWHD_GAME_DATA(0x1003140C,u8),actor);
}
