/* CC0 d_a_npc_mt.cpp makeFigures; MIT make_all_figurines_obtainable.asm.
 * Complete consumer, retaining captured count and wrapped byte selection index. */
#pragma once
WWHD_GAME_FUNC(0x022A037C,u32,randomizer_gallery_count,(u32 actor));
WWHD_GAME_FUNC(0x022A0898,u32,randomizer_gallery_photo,(u32 actor,u32 index));
WWHD_GAME_FUNC(0x0229FF88,void,randomizer_gallery_set_figure,(u32 actor,u32 figure));
WWHD_GAME_FUNC(0x025BD600,u32,randomizer_gallery_index,(u32 photo));
static void randomizer_gallery_add(u32 actor,u32 photo) {
    const u32 figure=randomizer_gallery_index(photo)&255;
    randomizer_gallery_set_figure(actor,figure);
}
WWHD_REPLACE(0x022A2078,void,randomizer_gallery_figures,(u32 actor)) {
    u32 remaining=randomizer_gallery_count(actor),index=0;
    while(remaining) {
        const u32 figure=randomizer_gallery_photo(actor,index);
        randomizer_gallery_set_figure(actor,figure);
        if(figure==(randomizer_gallery_index(0xBA)&255))randomizer_gallery_add(actor,0xC6);
        else if(figure==(randomizer_gallery_index(0x99)&255))randomizer_gallery_add(actor,0x9A);
        else if(figure==(randomizer_gallery_index(0xCE)&255))randomizer_gallery_add(actor,0xCA);
        else if(figure==(randomizer_gallery_index(0xC4)&255))randomizer_gallery_add(actor,0xC5);
        else if(figure==(randomizer_gallery_index(0x4A)&255))randomizer_gallery_add(actor,0x49);
        else if(figure==(randomizer_gallery_index(0x88)&255)) {
            for(u32 pirate=0x73;pirate<=0x78;pirate++)randomizer_gallery_add(actor,pirate);
            randomizer_gallery_add(actor,0x72);randomizer_gallery_add(actor,0x87);
        } else if(figure==(randomizer_gallery_index(0x8C)&255))randomizer_gallery_add(actor,0x8D);
        remaining--;index=(index+1)&255;
    }
}
