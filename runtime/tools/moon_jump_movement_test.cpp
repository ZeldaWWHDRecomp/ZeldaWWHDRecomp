// Exercise production Link hooks using synthetic state only.
#include "runtime.h"
#include "mods/mods.h"
#include <cassert>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
constexpr uint32_t actor=0x16000000;
static float factor=1;
static bool flying=false;
namespace mods {
float link_move_factor(uint32_t){return factor;}
void move_start_effect(Cpu*,uint32_t){} void move_trail_effect(Cpu*,uint32_t){} void move_swim_effect(Cpu*,uint32_t){}
SidleScope sidle_scope_begin(Cpu*,bool){return {};} void sidle_scope_end(const SidleScope&){}
bool moon_jump_move(uint32_t link,float& x,float& z){if(!flying || link!=actor)return false;x=40;z=-10;return true;}
}
extern "C" void site_023FD39C(Cpu*);
extern "C" void f_02416230_orig(Cpu*){}
static void map(uint32_t address,size_t size){
#ifdef _WIN32
 auto p=VirtualAlloc(mem::ptr(address),size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
 auto p=mmap(mem::ptr(address),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#endif
 assert(p==mem::ptr(address));
}
int main(){
 map(actor,0x10000);map(mem::kFixedStart,0x10000);
 Cpu cpu{};cpu.r[4]=actor+0x33C;cpu.f[31].ps0=10;
 stf32(cpu.r[4],3);stf32(cpu.r[4]+4,12);stf32(cpu.r[4]+8,4);
 site_023FD39C(&cpu);assert(cpu.r[4]==actor+0x33C); // exact stock argument retained
 flying=true;site_023FD39C(&cpu);
 assert(ldf32(cpu.r[4])==40 && ldf32(cpu.r[4]+4)==12 && ldf32(cpu.r[4]+8)==-10);
 // The run boost scales the horizontal speed only, with moon jump off.
 cpu.r[4]=actor+0x33C;flying=false;factor=2;site_023FD39C(&cpu);
 assert(ldf32(cpu.r[4])==6 && ldf32(cpu.r[4]+4)==12 && ldf32(cpu.r[4]+8)==8);
 // Off again: the stock argument.
 cpu.r[4]=actor+0x33C;factor=1;site_023FD39C(&cpu);assert(cpu.r[4]==actor+0x33C);
}
