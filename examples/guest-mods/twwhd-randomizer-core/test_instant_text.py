"""All eleven instant-text handlers, enabled/disabled and native callback seams."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class InstantText(unittest.TestCase):
 def test_handlers(self):
  code=r'''

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef uint16_t u16;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u32 init_instant_text_boxes;
static _Alignas(16) u8 window[0x200],message[0xA00],message2[0xA00],core[0xA00],play[0x6100],pane[0x100],icon[0x100],owner[0x100],animator[0x100],vtable[0x500],states[0x6000];static u32 coreptr=(u32)core;static u8 input_mode;
static void* guest_data(u32 address){if(address==0x101F4B5C)return&coreptr;if(address==0x1047B09E)return&input_mode;if(address>=0x10498000&&address<0x1049E500)return states+(address-0x10498000);assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)guest_data(a))
static struct{u32 key,value;}cells[128];static unsigned nc;
static u32* cell(u32 key){for(unsigned i=0;i<nc;i++)if(cells[i].key==key)return&cells[i].value;assert(nc<128);cells[nc].key=key;cells[nc].value=0;return&cells[nc++].value;}
#define RITPTR(a,o) (*cell((a)+(o)))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "instant_text.h"
static unsigned infos,has_calls,changes,panes,status_calls,sounds,ready_calls,animations,up_calls,down_calls,lines,virtuals;static unsigned has_result,ready_result,animation_result,up_result,down_result;static u32 changed_state,line_value;static unsigned last_status,last_sound;static unsigned ready_mutation,has_mutation,animation_mutation,change_mutation;static unsigned trace[32],nt;
static void log_call(unsigned value){assert(nt<32);trace[nt++]=value;}
static u32 wwhd_dComIfG_getGameInfo(void){++infos;log_call(1);return(u32)play;}
u32 rit_has_input(u32 s){assert(s==(u32)window);++has_calls;log_call(2);if(has_mutation)RITPTR(s,0x118)=(u32)message2;return has_result;}
void rit_change(u32 machine,u32 state){assert(machine==(u32)window+0x18||machine==(u32)icon+0x18);++changes;changed_state=state;log_call(3);if(change_mutation)input_mode=77;}
void rit_status(u32 c,u32 status){assert(c==(u32)core);++status_calls;last_status=status;log_call(4);}
void rit_pane(u32 p,u32 m){assert(p==(u32)pane&&(m==(u32)message||m==(u32)message2));++panes;log_call(5);}
u32 rit_selection_ready(u32 selector){assert(selector==42);++ready_calls;log_call(6);if(ready_mutation){RIT32((u32)window,0x54)=2;RIT32((u32)window,0xF4)=2;}return ready_result;}
u32 rit_sound(u32 sound){++sounds;last_sound=sound;log_call(7);return 0;}
s32 rit_can_down(u32 s){assert(s==(u32)window);++down_calls;log_call(8);return down_result;}
s32 rit_can_up(u32 s){assert(s==(u32)window);++up_calls;log_call(9);return up_result;}
void rit_lines(u32 s,u32 text){assert(s==(u32)window);++lines;line_value=text;log_call(10);}
u32 rit_animation_end(u32 a,u32 index){assert(a==(u32)animator&&(index==0||index==3));++animations;log_call(11);if(animation_mutation){RIT8((u32)icon,0x54)=1;RIT32((u32)icon,0x50)=3;}return animation_result;}
static void close_virtual(u32 s){assert(s==(u32)window);++virtuals;log_call(12);}
static void reset(unsigned enabled,unsigned held,unsigned edge){memset(window,0,sizeof window);memset(message,0,sizeof message);memset(message2,0,sizeof message2);memset(core,0,sizeof core);memset(play,0,sizeof play);memset(icon,0,sizeof icon);nc=0;init_instant_text_boxes=enabled;infos=has_calls=changes=panes=status_calls=sounds=ready_calls=animations=up_calls=down_calls=lines=virtuals=nt=0;has_result=ready_result=animation_result=up_result=down_result=0;changed_state=line_value=last_status=last_sound=0;ready_mutation=has_mutation=animation_mutation=change_mutation=0;input_mode=99;RIT32((u32)window,0x50)=held;RIT32((u32)window,0x54)=edge;RITPTR((u32)window,0x118)=(u32)message;RITPTR((u32)window,0x68)=(u32)pane;RITPTR((u32)window,4)=(u32)vtable;RITPTR((u32)vtable,0x4A4)=(u32)close_virtual;RITPTR((u32)core,0x964)=42;RITPTR((u32)icon,0x44)=(u32)owner;RITPTR((u32)owner,0xD4)=(u32)animator;RIT8((u32)play,0x5BB7)=RIT8((u32)play,0x5BBB)=255;}
static unsigned state(u32 address){return changed_state==RITDATA(address);}
int main(void){unsigned cases=0;
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned held=0;held<4;held++)for(unsigned edge=0;edge<4;edge++)for(unsigned confirm=0;confirm<2;confirm++)for(unsigned close=0;close<2;close++)for(unsigned next=0;next<2;next++){
 reset(enabled,held,edge);has_result=confirm;RIT8((u32)message,0x90E)=close;RIT32((u32)window,0x84)=0xFFFFFFFFu;RITS32((u32)window,0x9C)=next?0:-1;randomizer_text_stop((u32)window);unsigned accept=confirm||((enabled?held:edge)&2);if(accept){assert(state(next?0x1049E3AC:0x1049E2BC)&&RIT32((u32)window,0x84)==0&&RIT8((u32)play,0x5BD2)==0&&input_mode==2&&status_calls==!next);}else if(close)assert(state(0x1049E40C)&&last_status==1);else assert(panes==1&&!changes&&input_mode==1);assert(has_calls==1);++cases;
}
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned held=0;held<4;held++)for(unsigned edge=0;edge<4;edge++)for(unsigned confirm=0;confirm<2;confirm++)for(unsigned blocked=0;blocked<2;blocked++)for(int timer=-1;timer<3;timer++)for(unsigned next=0;next<2;next++){
 reset(enabled,held,edge);has_result=confirm;RIT8((u32)play,0x5C20)=blocked;RITS32((u32)message,0x6B4)=timer;RIT8((u32)message,0x698)=1;RITS32((u32)window,0x9C)=next?0:-1;randomizer_text_wait_input((u32)window);unsigned accept=!timer||((confirm||((enabled?held:edge)&2))&&!blocked);if(accept)assert(state(next?0x1049E3AC:0x1049E2BC)&&!RIT8((u32)message,0x698)&&RIT32((u32)window,0x84)==1&&input_mode==(timer?2:0));else assert(!changes&&panes==1&&RITS32((u32)message,0x6B4)==(timer>0?timer-1:0)&&input_mode==1);++cases;
}
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned held=0;held<4;held++)for(unsigned edge=0;edge<4;edge++)for(unsigned confirm=0;confirm<2;confirm++)for(unsigned message_confirm=0;message_confirm<2;message_confirm++)for(unsigned blocked=0;blocked<2;blocked++)for(int timer=-1;timer<2;timer++){
 reset(enabled,held,edge);has_result=confirm;RIT8((u32)message,0x90D)=message_confirm;randomizer_text_auto_stop_input((u32)window);unsigned input=confirm||((enabled?held:edge)&2)||message_confirm;assert(changes==!!input&&panes==!input&&input_mode==(input?0:4));if(input)assert(state(0x1049E43C));++cases;
 reset(enabled,held,edge);has_result=confirm;RIT8((u32)message,0x90D)=message_confirm;RIT8((u32)play,0x5C20)=blocked;RITS32((u32)message,0x6B4)=timer;RIT8((u32)message,0x698)=1;randomizer_text_auto_wait_input((u32)window);if(!timer)assert(state(0x1049E34C)&&RIT32((u32)window,0x84)==1&&!has_calls);else if(input&&!blocked)assert(state(0x1049E43C)&&!RIT32((u32)window,0x84)&&!RIT8((u32)message,0x698));else assert(!changes&&panes==1&&RITS32((u32)message,0x6B4)==(timer>0?timer-1:0));++cases;
 reset(enabled,held,edge);has_result=confirm;randomizer_text_stop_input((u32)window);unsigned stop=confirm||((enabled?held:edge)&2);assert(changes==!!stop&&has_calls==1);if(stop)assert(state(0x1049E07C));++cases;
}
/* Later selection input reads remain edge-based even when the first read uses held. */
const unsigned moves[]={0,0x100000,0x200000,0x10000,0x20000,0x330000};
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned held=0;held<4;held++)for(unsigned edge=0;edge<4;edge++)for(unsigned confirm=0;confirm<2;confirm++)for(unsigned msgconfirm=0;msgconfirm<2;msgconfirm++)for(unsigned ready=0;ready<2;ready++)for(unsigned mode=0;mode<2;mode++)for(unsigned old=0;old<4;old++)for(unsigned movement=0;movement<6;movement++)for(unsigned three=0;three<2;three++){
 reset(enabled,held,edge|moves[movement]);has_result=confirm;ready_result=ready;RIT8((u32)message,0x90D)=msgconfirm;RIT8((u32)play,0x5BB6)=mode?39:0;RIT32((u32)window,0xF4)=old==3?0xFFFFFFFFu:old;unsigned initial=RIT32((u32)window,0xF4);if(three)randomizer_text_select_three((u32)window);else randomizer_text_select_two((u32)window);unsigned accept=(confirm||(((enabled?held:edge)&2)&&mode)||msgconfirm)&&ready;if(accept){assert(state(0x1049E10C)&&input_mode==2&&RIT32((u32)core,0x948)==initial&&RIT8((u32)play,0x5BD3)==!!(edge&2)&&last_status==14);assert(last_sound==(three?(initial==2?0x880:0x80D):(initial?0x880:0x80D)));}else{unsigned expected=initial;if(movement){unsigned previous=movement==1||movement==3||movement==5;if(!three)expected=previous?0:1;else if(previous){if((s32)initial>0)expected=initial-1;}else if((s32)initial<2)expected=initial+1;}assert(!changes&&RIT32((u32)window,0xF4)==expected&&input_mode==1);assert(sounds==(expected!=initial));}++cases;
}
/* Byte sound mode has a deliberately nonzero neighboring byte. */
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned input=0;input<2;input++)for(unsigned blocked=0;blocked<2;blocked++)for(unsigned soundmode=0;soundmode<2;soundmode++){
 reset(enabled,input?2:0,input?2:0);RIT8((u32)play,0x5C20)=blocked;RIT8((u32)play,0x5BBA)=soundmode?39:0;randomizer_text_hand_stop((u32)window);if(input&&!blocked)assert(state(0x1049E3DC)&&last_sound==(soundmode?0x880:0x80B)&&infos==4&&RIT32((u32)window,0x84)==1);else assert(!changes&&panes==1);++cases;
}
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned blocked=0;blocked<2;blocked++)for(unsigned closeblocked=0;closeblocked<2;closeblocked++)for(unsigned same=0;same<2;same++){
 reset(enabled,0,0);RIT8((u32)message,0x90E)=1;RIT8((u32)message,0x90F)=closeblocked;RIT32((u32)message,0x910)=same?0:1;RIT8((u32)play,0x5C20)=blocked;randomizer_text_hand_stop((u32)window);unsigned closing=!blocked&&!closeblocked&&!same;assert(virtuals==closing);if(closing)assert(state(0x1049DE9C)&&last_sound==0x80B);else assert(panes==1);++cases;
}
/* Scroll snapshot, priority, clamped line pointer and callback ordering. */
const unsigned scroll_flags[]={0,1,2,0x10000,0x100000,0x20000,0x200000,0x330003};
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned h=0;h<8;h++)for(unsigned e=0;e<8;e++)for(unsigned up=0;up<2;up++)for(unsigned down=0;down<2;down++)for(unsigned line=0;line<22;line++){
 reset(enabled,scroll_flags[h],scroll_flags[e]);up_result=up;down_result=down;RIT32((u32)window,0x118)=line;for(unsigned i=0;i<20;i++)RITPTR((u32)window,0xC8+4*i)=100+i;unsigned flags=scroll_flags[enabled?h:e];randomizer_instant_scroll_input((u32)window);if(flags&3)assert(state(down?0x10498C48:0x10498D08)&&input_mode==2);else if(flags&0x110000){assert(changes==up);if(up)assert(state(0x10498C78)&&input_mode==(down?2:0));}else if(flags&0x220000)assert(state(down?0x10498C48:0x10498D08));else assert(lines==1&&line_value==100+(line<20?line:0)&&input_mode==(down?1:4));++cases;
}
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned ended=0;ended<2;ended++){
 reset(enabled,0,0);animation_result=ended;randomizer_instant_icon_in((u32)icon);assert(animations==1&&changes==(enabled||ended));if(changes)assert(state(0x1049DCAC));++cases;
}
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned wait=0;wait<2;wait++)for(unsigned ended=0;ended<2;ended++)for(unsigned request=0;request<5;request++)for(unsigned mutation=0;mutation<2;mutation++){
 reset(enabled,0,0);RIT8((u32)icon,0x54)=wait;RIT32((u32)icon,0x50)=request;animation_result=ended;animation_mutation=mutation;randomizer_instant_icon_decide((u32)icon);unsigned restart=(mutation?3:request)==3;if(wait){assert(changes==(ended||restart));if(changes)assert(state(ended?0x1049DCAC:0x1049DCDC));}else{assert(changes==(enabled||ended));if(changes)assert(state(0x1049DD0C));}assert(animations==1);++cases;
}
/* Native later edge/selection reads occur after the selector callback. */
reset(1,2,0);RIT8((u32)play,0x5BB6)=39;ready_result=ready_mutation=1;randomizer_text_select_two((u32)window);assert(state(0x1049E10C)&&RIT32((u32)core,0x948)==2&&RIT8((u32)play,0x5BD3)==1&&last_sound==0x880);++cases;
/* Captured old message is used for confirm; accepted input then reloads the live message. */
reset(1,0,0);has_mutation=1;RITS32((u32)message,0x6B4)=5;RIT8((u32)message,0x698)=1;RIT8((u32)message,0x90D)=1;RIT8((u32)message2,0x698)=1;randomizer_text_auto_wait_input((u32)window);assert(state(0x1049E43C)&&!RIT8((u32)message2,0x698)&&RIT8((u32)message,0x698)==1);++cases;
reset(1,0,0);has_mutation=1;RITS32((u32)message,0x6B4)=5;RIT8((u32)message2,0x90D)=1;randomizer_text_auto_wait_input((u32)window);assert(!changes&&panes==1&&RITS32((u32)message,0x6B4)==4);++cases;
reset(1,0,0);has_mutation=1;has_result=1;RITS32((u32)message,0x6B4)=5;RIT8((u32)message,0x698)=1;RIT8((u32)message2,0x698)=1;randomizer_text_wait_input((u32)window);assert(!RIT8((u32)message2,0x698)&&RIT8((u32)message,0x698)==1&&state(0x1049E3AC));++cases;
reset(1,0,0);RIT8((u32)message,0x90E)=1;RIT32((u32)message,0x910)=1;randomizer_text_hand_stop((u32)window);assert(nt==5&&trace[0]==2&&trace[1]==1&&trace[2]==7&&trace[3]==12&&trace[4]==3);++cases;
reset(1,1,0);down_result=1;change_mutation=1;randomizer_instant_scroll_input((u32)window);assert(state(0x10498C48)&&input_mode==2);++cases;
printf("Instant text: %u complete-handler cases passed\n",cases);return 0;}

'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
