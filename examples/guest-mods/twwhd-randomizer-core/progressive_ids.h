/* Progressive item selection ported from TWWHD Randomizer 1.2.0 custom_funcs.asm.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt. */
#pragma once
#define RANDO_NO_PROGRESSIVE_FIELD 0xFFFFFFFFu
#define RANDO_NO_ITEM 0xFFu

/* Fields are relative to the save allocation, including its 0x20-byte prefix. */
static inline unsigned rando_progressive_field(unsigned item) {
    switch(item) {
    case 0x38:case 0x39:case 0x3A:case 0x3D:case 0x3E:return 0xD4;
    case 0x3B:case 0x3C:return 0xD5;
    case 0x27:case 0x35:case 0x36:return 0x7D;
    case 0xAB:case 0xAC:return 0x32;
    case 0xAD:case 0xAE:return 0x90;
    case 0xAF:case 0xB0:return 0x8F;
    case 0x23:case 0x26:return 0x79;
    case 0x78:case 0x77:return 0x5D;
    case 0xB1:case 0xB2:return 0x33;
    default:return RANDO_NO_PROGRESSIVE_FIELD;
    }
}

/* Return a concrete native item ID, or NO_ITEM when the progression is capped/invalid.
 * B1 uses the mod's base-magic handler because the game's original handler is empty. */
static inline unsigned rando_progressive_next(unsigned item,unsigned state,unsigned always_double_magic) {
    switch(rando_progressive_field(item)) {
    case 0xD4:return state==0?0x38:state==1?0x39:state==3?0x3A:state==7?0x3E:RANDO_NO_ITEM;
    case 0xD5:return state==0?0x3B:state==1?0x3C:RANDO_NO_ITEM;
    case 0x7D:return state==0?0x27:state==1?0x35:state==3?0x36:RANDO_NO_ITEM;
    case 0x32:return state==0?0xAB:state==1?0xAC:RANDO_NO_ITEM;
    case 0x90:return state==30?0xAD:state==60?0xAE:RANDO_NO_ITEM;
    case 0x8F:return state==30?0xAF:state==60?0xB0:RANDO_NO_ITEM;
    case 0x79:return state==0?0x23:state==1?0x26:RANDO_NO_ITEM;
    case 0x5D:return state==255?0x78:state==0x78?0x77:RANDO_NO_ITEM;
    case 0x33:return always_double_magic||state==16?0xB2:state==0?0xB1:RANDO_NO_ITEM;
    default:return item;
    }
}

/* Presentation follows upstream's base-model fallback when an upgrade is capped
 * or its state is invalid. Grants still do nothing in those states. */
static inline unsigned rando_progressive_display(unsigned item,unsigned state,unsigned always_double_magic) {
    const unsigned next=rando_progressive_next(item,state,always_double_magic);
    if(next!=RANDO_NO_ITEM) return next;
    switch(rando_progressive_field(item)) {
    case 0xD4:return 0x38;
    case 0xD5:return 0x3B;
    case 0x7D:return 0x27;
    case 0x32:return 0xAB;
    case 0x90:return 0xAD;
    case 0x8F:return 0xAF;
    case 0x79:return 0x23;
    case 0x5D:return 0x78;
    case 0x33:return 0xB1;
    default:return item;
    }
}
