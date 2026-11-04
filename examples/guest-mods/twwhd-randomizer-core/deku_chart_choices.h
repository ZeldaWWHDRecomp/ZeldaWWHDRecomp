/* CC0 d_a_npc_de1.cpp next_msgStatus; MIT chart-marking flag/branch change. */
#pragma once
WWHD_REPLACE(0x0222C544,u32,randomizer_deku_chart_choices,(u32 actor,u32 message_pointer)) {
    (void)actor;const u32 message=*(volatile unsigned int*)message_pointer;
    const u32 manager=WWHD_GAME_DATA(0x101F4B5C,u32);
    u32 next;
    switch(message) {
    case 0x139D:next=0x139E;break;
    case 0x139F:next=0x13A0;break;
    case 0x13A1:next=0x13A2;break;
    case 0x13A3:next=0x13A4;break;
    case 0x13A4:next=0x13A5;break;
    case 0x13A5:next=0x13A6;break;
    case 0x13A8:case 0x13AC:next=0x13A9;break;
    case 0x13A9:case 0x13C5:
        next=wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x3940)==1?0x13AB:0x13AA;break;
    case 0x13AA: {
        const u32 selection=*(volatile unsigned int*)(manager+0x948);
        if(!selection)next=0x13AD;
        else if(selection==1)next=wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x0102)?0x13D2:0x13C7;
        else next=0x13C6;
        break;
    }
    case 0x13AB: {
        const u32 selection=*(volatile unsigned int*)(manager+0x948);
        next=!selection?0x13AD:selection==1?0x13CC:0x13C6;break;
    }
    case 0x13AD: {
        const u32 selection=*(volatile unsigned int*)(manager+0x948);
        if(!selection)next=wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x0102)?0x13B0:0x13AE;
        else next=selection==1?0x13B6:0x13C6;
        break;
    }
    case 0x13AE:case 0x13AF:case 0x13B0:case 0x13C1:case 0x13C3:case 0x13C4:next=0x13C5;break;
    case 0x13B6:
        next=wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x1820)?(randomizer_daily_boss(6)?0x13C2:0x13C4):0x13C0;break;
    case 0x13C0:next=0x13C1;break;
    case 0x13C2:next=0x13C3;break;
    case 0x13C7:next=0x13C8;break;
    case 0x13C8:next=0x13C9;break;
    case 0x13C9:next=0x13CA;break;
    case 0x13CA:
        *(volatile unsigned int*)message_pointer=0x13CB;
        wwhd_dSv_event_c_onEventBit(randomizer_daily_event(),0x3940);return 15;
    case 0x13CC:next=0x13CD;break;
    case 0x13CD:next=0x13CE;break;
    case 0x13CE:next=0x13CF;break;
    case 0x13CF:next=0x13D0;break;
    case 0x13D0:next=0x13D1;break;
    default:return 16;
    }
    *(volatile unsigned int*)message_pointer=(unsigned int)next;return 15;
}
