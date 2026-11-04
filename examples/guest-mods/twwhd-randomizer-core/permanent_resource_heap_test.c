/* Synthetic full-consumer callbacks and heap/range lifecycle tests. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;
static unsigned char memory[0x500000];
static void *host_at(u32 a){if(a>UINT32_MAX)return(void*)a;assert((a&0xFFFFFF)<sizeof memory-8);return memory+(a&0xFFFFFF);}
typedef uint32_t m_u32;typedef uint8_t m_u8;
#define R_PERM_HEAP(t,a) (*(volatile m_##t*)host_at(a))
#define PERM_HEAP_DATA(a) ((u32)(a))
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u32 call1(u32,u32);static u32 call2(u32,u32,u32);static u32 call3(u32,u32,u32,u32);
#define PERM_HEAP_CALL1(t,a) call1(t,a)
#define PERM_HEAP_CALL2(t,a,b) call2(t,a,b)
#define PERM_HEAP_CALL3(t,a,b,c) call3(t,a,b,c)
#include "permanent_resource_heap.h"
static const u32 self=0x80000;
static unsigned mode,trace[256],count,heaps,archives,appends,mounts,phases,tasks,finds,constructs;
static u32 config_frame;
static void record(unsigned n){assert(count<256);trace[count++]=n;}
static u32 call3(u32 t,u32 a,u32 b,u32 c){assert(t==0xAA01&&a==0xA000&&b==self);config_frame=c-0x70;assert(R_PERM_HEAP(u32,c)==0x104612BC&&!R_PERM_HEAP(u32,c+8)&&!R_PERM_HEAP(u32,c+12));R_PERM_HEAP(u32,self+0x5C)=0x9100;record(100);return 0;}
static u32 call1(u32 t,u32 a){assert(t==0xAA04&&a==0xB800);record(101);return 0xDEAD;}
static u32 call2(u32 t,u32 a,u32 b){assert(t==0xAA03&&a==0xE000+tasks*0x100);assert(b==(tasks==1?0x101FD9D4:0x101FD9D8));record(102);return 0;}
void perm_hdRootProcessInitialize(void){record(1);}
void perm_hdRootSystemInitialize(void){record(2);}
u32 perm_hdRootGetProcess(void){record(3);return 0xAB00+phases;}
void perm_hdRootSetProcessPhase(u32 a0,u32 a1){record(4);(void)a0;(void)a1;assert(a0==0xAB00+phases&&a1==phases);phases++;}
void perm_WPADEnableURCC(u32 a0){record(5);(void)a0;assert(a0==1);}
void perm_WPADEnableWiiRemote(u32 a0){record(6);(void)a0;assert(!a0);}
void perm_hdRootInputInitialize(u32 a0){record(7);(void)a0;}
u32 perm_hdRootLoadArchive(u32 a0,u32 a1,u32 a2,u32 a3){record(8);(void)a0;(void)a1;(void)a2;(void)a3;assert(!a0&&a2==0xA100);const u32 names[4][2]={{0x1000659C,0x10006624},{0x10006630,0x1000663C},{0x100065A8,0x10006590},{0x10006598,0x100065A4}};assert(R_PERM_HEAP(u32,a1)==names[archives][0]&&R_PERM_HEAP(u32,a3)==names[archives][1]);R_PERM_HEAP(u32,0x101F8B08)=0xA220;R_PERM_HEAP(u32,0xA23C)=0xBEEF;return 0xAC00+archives++*0x100;}
u32 perm_hdRootCreateMultiArchive(u32 a0,u32 a1,u32 a2,u32 a3){record(9);(void)a0;(void)a1;(void)a2;(void)a3;assert(a0==0&&a1==4&&a2==0&&a3==4);return 0xAD00;}
void perm_hdRootAppendArchive(u32 a0,u32 a1){record(10);(void)a0;(void)a1;assert(a0==0xAD00&&a1==0xAC00+appends*0x100);appends++;}
u32 perm_hdRootCreateMount(u32 a0,u32 a1){record(11);(void)a0;(void)a1;assert(!a0&&a1==(mounts?7:255));return 0xAE00;}
void perm_hdRootMountDevice(u32 a0,u32 a1,u32 a2){record(12);(void)a0;(void)a1;(void)a2;assert(a0==0xA220&&a1==0xAE00);assert(R_PERM_HEAP(u32,a2)==(mounts?0x1000664C:0x100065C0));mounts++;}
void perm_nn_act_Initialize(void){record(13);}
u32 perm_nn_act_GetSlotNo(void){record(14);return 7;}
void perm_hdRootHeapInitialize(u32 a0){record(15);(void)a0;}
void perm_hdRootSelectDefaultHeap(u32 a0,u32 a1,u32 a2){record(16);(void)a0;(void)a1;(void)a2;assert(a0==0xA300&&a1==0xA400&&R_PERM_HEAP(u32,a2)==0x10006658);}
u32 perm_hdRootGetTaskInfo(u32 a0){record(17);(void)a0;assert(a0==self);return 0xA500;}
void perm_hdRootInputConfigure(u32 a0,u32 a1,u32 a2){record(18);(void)a0;(void)a1;(void)a2;}
u32 perm_hdRootGetMainHeap(void){record(19);return 0xA700;}
u32 perm_hdRootCreateExpHeap(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4){record(20);(void)a0;(void)a1;(void)a2;(void)a3;(void)a4;const u32 sizes[]={0xB00000,0xE00100,0x15DFFF00,0xD450200,0x100000,0x2000000,0x5C00000};const u32 names[]={0x100065B0,0x100065CC,0x10006688,0x100065DC,0x100065B8,0x10006600,0x100066B8};assert(heaps<7&&a0==sizes[heaps]&&R_PERM_HEAP(u32,a1)==names[heaps]);assert(a2==(heaps==3?0xB200:heaps<3?0xA700:heaps==4?0xA800:0xA900));assert(a3==(heaps==3?0xFFFFFFFFu:1)&&a4==(heaps==2));return 0xB000+heaps++*0x100;}
void perm_hdRootResourceInitialize(u32 a0){record(21);(void)a0;}
void perm_hdRootResourceSetHeap(u32 a0,u32 a1){record(22);(void)a0;(void)a1;}
u32 perm_hdRootHeapOverhead(u32 a0){record(23);(void)a0;return a0==0x2000?0x200:0x100;}
void perm_hdRootModelInitialize(u32 a0){record(24);(void)a0;}
void perm_hdRootSaveInitialize(u32 a0,u32 a1){record(25);(void)a0;(void)a1;assert(a0==0xC000&&a1==0xB200);R_PERM_HEAP(u32,0x101F4F28)=0xC100;}
u32 perm_hdRootGetArchiveHeap(void){record(26);return 0xA800;}
void perm_hdRootLoadArchiveResource(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,u32 a5,u32 a6){record(27);(void)a0;(void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;}
u32 perm_hdRootGetArchiveResource(u32 a0,u32 a1,u32 a2,u32 a3){record(28);(void)a0;(void)a1;(void)a2;(void)a3;return 0xD000;}
u32 perm_hdRootCastArchive(u32 a0){record(29);(void)a0;assert(a0==0xD000);return 0xD100;}
u32 perm_hdRootGetGraphicsHeap(void){record(30);return 0xA900;}
void perm_hdRootUiGraphicsInitialize(u32 a0){record(31);(void)a0;}
u32 perm_hdRootFindArchiveFile(u32 a0,u32 a1){record(32);(void)a0;(void)a1;assert(a0==(finds?0xD15C:mode&8?0:0xD16C));assert(a1==(finds?0x1000660C:0x100066A0));R_PERM_HEAP(u32,0xD14C)=0x10;return finds++?0xD300:0xD200;}
void perm_hdRootSetShaderRanges(u32 a0,u32 a1,u32 a2,u32 a3){record(33);(void)a0;(void)a1;(void)a2;(void)a3;assert(a0==0xD400&&a1==0xB500);assert(R_PERM_HEAP(u32,a2)==(mode&1?0:0xD220)&&R_PERM_HEAP(u32,a2+4)==(mode&1?0:0xD220)+0x30);assert(R_PERM_HEAP(u32,a3)==(mode&2?0:0xD340)&&R_PERM_HEAP(u32,a3+4)==(mode&2?0:0xD340)+0x50);}
void perm_hdRootUiResourceInitialize(u32 a0){record(34);(void)a0;}
void perm_hdRootUiResourceSetHeap(u32 a0,u32 a1){record(35);(void)a0;(void)a1;}
u32 perm_hdRootGetFontHeap(void){record(36);return 0xAB00;}
void perm_hdRootFontInitialize(u32 a0){record(37);(void)a0;}
void perm_hdRootFontSetHeap(u32 a0,u32 a1){record(38);(void)a0;(void)a1;}
u32 perm_hdRootCreateFrameHeap(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4){record(39);(void)a0;(void)a1;(void)a2;(void)a3;(void)a4;assert(!a0&&a2==0xAB00&&a3==1&&!a4);return 0xB800;}
u32 perm_hdRootCreateFontResource(u32 a0){record(40);(void)a0;assert(a0==0xB800);R_PERM_HEAP(u32,0xB80C)=0xB940;R_PERM_HEAP(u32,0xB96C)=0xAA04;return 0xD500;}
void perm_hdRootRegisterFontResource(u32 a0){record(41);(void)a0;assert(a0==0xD500);}
void perm_hdRootMiiverseInitialize(u32 a0){record(42);(void)a0;}
void perm_hdRootMiiverseConfigure(u32 a0,u32 a1){record(43);(void)a0;(void)a1;}
void perm_hdRootTaskArgsConstruct(u32 a0,u32 a1){record(44);(void)a0;(void)a1;assert(R_PERM_HEAP(u32,a1)==2&&R_PERM_HEAP(u32,a1+4)==0x0203FAF4);for(unsigned i=0;i<32;i++)R_PERM_HEAP(u32,a0+i*4)=0x1100+i;R_PERM_HEAP(u32,a0+0x58)=2;constructs++;}
u32 perm_hdRootGetChildHeap(void){record(45);return 0xD600;}
u32 perm_hdRootCreateTask(u32 a0,u32 a1){record(46);(void)a0;(void)a1;assert(a0==(tasks?0x9200:0x9100));R_PERM_HEAP(u32,self+0x5C)=0x9200;tasks++;for(unsigned i=0;i<32;i++){u32 expected=(tasks==1?0x1100:0x2200)+i;if(i==31)expected=tasks==1?0x02760FE4:0x02729488;if(tasks==1){if(i==22)expected=2;if(i==12)expected=0xD600;if(i==13)expected=0x3FFFF00;if(i==23)expected=self;if(i==24)expected=0xD700;}assert(R_PERM_HEAP(u32,a1+i*4)==expected);}return mode&4?0:0xE000+tasks*0x100;}
void perm_hdRootBasePrepare(u32 a0){record(47);(void)a0;}
void perm_hdRootTaskArgsConstructAlternate(u32 a0,u32 a1){record(48);(void)a0;(void)a1;assert(R_PERM_HEAP(u32,a1)==2&&R_PERM_HEAP(u32,a1+4)==0x0203FB48);for(unsigned i=0;i<32;i++)R_PERM_HEAP(u32,a0+i*4)=0x2200+i;constructs++;}
void perm_graphics_heaps(void){record(49);}
int main(void){for(mode=0;mode<64;mode++){
 memset(memory,0,sizeof memory);count=heaps=archives=appends=mounts=phases=tasks=finds=constructs=0;
 R_PERM_HEAP(u32,0x104612B8)=!!(mode&16);R_PERM_HEAP(u32,0x104612D8)=0xFEABCDEF;R_PERM_HEAP(u32,0x104612D0)=0x1122;R_PERM_HEAP(u32,0x104612BC)=0x3344;R_PERM_HEAP(u32,0x101FD59C)=!!(mode&32);R_PERM_HEAP(u32,0x101FD71C)=!!(mode&32);R_PERM_HEAP(u32,0x101FD9D4)=0xFAFA;R_PERM_HEAP(u32,0x101FD9D8)=0xFBFB;R_PERM_HEAP(u32,self+0x5C)=0x9000;R_PERM_HEAP(u32,0x9040)=0xA000;R_PERM_HEAP(u32,0xA024)=0xA040;R_PERM_HEAP(u32,0xA06C)=0xAA01;
 R_PERM_HEAP(u32,0x101F8B08)=0xA200;R_PERM_HEAP(u32,0xA21C)=0xA100;R_PERM_HEAP(u32,0x101F8B28)=0xA300;R_PERM_HEAP(u32,self+0x44)=0xA400;
 R_PERM_HEAP(u32,0xA51C)=0xA600;R_PERM_HEAP(u32,0x101F4F28)=0xC000;R_PERM_HEAP(u32,0x1018C404)=0xD400;
 R_PERM_HEAP(u32,0xD14C)=mode&8?0:0x20;R_PERM_HEAP(u32,0xD200)=mode&1?0:0x20;R_PERM_HEAP(u32,0xD204)=0x30;R_PERM_HEAP(u32,0xD300)=mode&2?0:0x40;R_PERM_HEAP(u32,0xD304)=0x50;
 R_PERM_HEAP(u32,0xB80C)=0xB900;R_PERM_HEAP(u32,0xB92C)=0xAA02;
 R_PERM_HEAP(u32,0xE170)=0xE300;R_PERM_HEAP(u32,0xE270)=0xE300;R_PERM_HEAP(u32,0xE30C)=0xAA03;
 R_PERM_HEAP(u32,0x104612E0)=0xD800;R_PERM_HEAP(u32,0xD814)=0xD700;
 randomizer_permanent_resource_prepare(self);
 /* Callback IDs follow the declarations above; sequence independently audited
  * against the canonical consumer, including four virtual endpoints. */
 const unsigned expected_trace[]={1,2,3,4,5,6,100,5,6,7,8,8,8,8,9,10,10,10,10,11,12,13,14,11,12,15,16,17,49,18,19,20,21,22,23,19,20,19,20,23,20,24,25,26,20,27,28,29,30,20,31,32,32,33,30,20,34,35,36,37,38,39,40,101,41,42,43,44,45,23,46,102,47,48,46,102,3,4};unsigned n=0;for(unsigned i=0;i<sizeof(expected_trace)/sizeof(*expected_trace);i++){if(expected_trace[i]==102&&(mode&4))continue;assert(n<count&&trace[n++]==expected_trace[i]);}assert(n==count);
 assert(heaps==7&&archives==4&&appends==4&&mounts==2&&phases==2&&tasks==2&&finds==2&&constructs==2);
 assert(R_PERM_HEAP(u32,0xC000+0x2044)==0xB100&&R_PERM_HEAP(u32,0xC000+0x2040)==0xB200&&R_PERM_HEAP(u32,0xC000+0x2128)==0xB300);
 assert(R_PERM_HEAP(u32,0x104612D8)==(((mode&16?0xFEABCDEF:0x140D)&0xFFFFFFEEu)|0x20));assert(R_PERM_HEAP(u32,0x104612D0)==(mode&16?0x1122:1));assert(R_PERM_HEAP(u32,0x104612BC)==(mode&16?0x3344:0x100064B8));assert(R_PERM_HEAP(u32,0x101FD9D4)==(mode&32?0xFAFA:0x10006458)&&R_PERM_HEAP(u32,0x101FD9D8)==(mode&32?0xFBFB:0x10006448));
 assert(R_PERM_HEAP(u32,0x101FD59C)==1&&R_PERM_HEAP(u32,0x101FD71C)==1);
 }
 return 0;}
