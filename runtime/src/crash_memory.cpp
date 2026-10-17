#include "crash_memory.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#ifdef __APPLE__
#include <mach/mach.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif
namespace crash_memory {
namespace {
constexpr size_t maxBytes=16*1024*1024, maxSections=64, summaryCapacity=1024;
struct Image { std::vector<Section> sections; };
std::atomic<const Image*> image{nullptr};
std::atomic<bool> active{false};
std::atomic<const uint8_t*> guestBase{nullptr};
static_assert(std::atomic<const uint8_t*>::is_always_lock_free);
std::atomic<unsigned> revision{0},summarySize{0};
std::atomic<char> summary[summaryCapacity];
static_assert(std::atomic<const Image*>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<char>::is_always_lock_free);
// Fixed local formatter: no libc formatting, allocation, locale, or locks in note().
struct Line {
 char data[256];size_t size=0;
 void text(const char* s){while(*s && size<sizeof data)data[size++]=*s++;}
 void hex(uint32_t v){for(int i=7;i>=0;--i)if(size<sizeof data)data[size++]="0123456789ABCDEF"[(v>>(4*i))&15];}
 void byte(uint8_t v){if(size+2<=sizeof data){data[size++]="0123456789ABCDEF"[v>>4];data[size++]="0123456789ABCDEF"[v&15];}}
 void send(int fd,Output out){out(fd,data,size);size=0;}
};
}
bool enabled(){
 static const bool value=[] {const char* p=std::getenv("WWHD_CRASH_GUEST_MEMORY");return p && !std::strcmp(p,"1");}();
 return value;
}
void install(std::vector<Section> sections,const uint8_t* live_guest_base){
 if(!enabled())return;
 // Reject all overlaps, even executable/data aliases. Do not admit wrapping, low,
 // high, non-executable, writable, or oversized sections from a malformed image.
 auto* next=new Image;size_t bytes=0;
 for(size_t i=0;i<sections.size();++i){
  const auto& s=sections[i];uint64_t end=uint64_t(s.address)+s.bytes.size();
  if((s.flags&7)!=6 || s.address<0x02000000 || end>0x10000000 || s.bytes.empty() || s.bytes.size()>maxBytes-bytes || next->sections.size()==maxSections)continue;
  bool overlap=false;
  for(size_t j=0;j<sections.size();++j)if(i!=j){const auto& t=sections[j];overlap|=uint64_t(s.address)<uint64_t(t.address)+(t.extent?t.extent:t.bytes.size()) && uint64_t(t.address)<end;}
  if(overlap)continue;
  bytes+=s.bytes.size();next->sections.push_back(s);
 }
 // Images intentionally remain alive until process exit: a signal reader may be
 // holding a prior publication. Installation is startup-only, not a per-frame cache.
 image.store(next,std::memory_order_release);
 guestBase.store(live_guest_base,std::memory_order_release);
 active.store(true,std::memory_order_release);
}
void refresh(uint32_t guest_threads,uint32_t runtime_top){
 if(!active.load(std::memory_order_acquire))return;
 uint64_t rss=0;bool available=false;
#ifdef __APPLE__
 mach_task_basic_info info{};mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
 if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS){rss=info.resident_size;available=true;}
#elif defined(_WIN32)
 PROCESS_MEMORY_COUNTERS info{};if(GetProcessMemoryInfo(GetCurrentProcess(),&info,sizeof info)){rss=info.WorkingSetSize;available=true;}
#else
 if(FILE* f=std::fopen("/proc/self/status","r")){char line[160];while(std::fgets(line,sizeof line,f)){unsigned long long kb;if(std::sscanf(line,"VmRSS: %llu kB",&kb)==1){rss=kb*1024;available=true;break;}}std::fclose(f);}
#endif
 char text[summaryCapacity];int n=std::snprintf(text,sizeof text,
  "guest memory summary (latest normal-thread snapshot): reserved=00010000..FFFFFFFF (not resident bytes or permissions); null guard=00000000..0000FFFF\n"
  "  MEM2=10000000..4FFFFFFF runtime=60000000..6FFFFFFF runtime_top=%08X foreground=E0000000..E27FFFFF MEM1=F4000000..F5FFFFFF\n"
  "  registered_guest_threads=%u process_rss_bytes=%llu rss_available=%u\n",runtime_top,guest_threads,(unsigned long long)rss,unsigned(available));
 unsigned length=n<0?0:unsigned(std::min<size_t>(n,sizeof text-1));
 revision.fetch_add(1,std::memory_order_seq_cst);
 for(unsigned i=0;i<length;++i)summary[i].store(text[i],std::memory_order_seq_cst);
 summarySize.store(length,std::memory_order_seq_cst);revision.fetch_add(1,std::memory_order_seq_cst);
}
void note(int fd,Output out,const uint32_t* registers){
 if(!active.load(std::memory_order_acquire))return;
 Line line;line.text("\n--- guest memory: verified executable baseline bytes only; all other live data redacted ---\n");line.send(fd,out);
 const Image* view=image.load(std::memory_order_acquire);
 if(view)for(const auto& section:view->sections){line.text("  trusted baseline code range ");line.hex(section.address);line.text("..");line.hex(uint32_t(uint64_t(section.address)+section.bytes.size()-1));line.text(" bytes=");line.hex(uint32_t(section.bytes.size()));line.text("\n");line.send(fd,out);}
 if(registers)for(unsigned reg=0;reg<32;++reg){
  const uint32_t address=registers[reg];const Section* section=nullptr;
  if(view)for(const auto& s:view->sections)if(address>=s.address && uint64_t(address)+128<=uint64_t(s.address)+s.bytes.size()){section=&s;break;}
  line.text("  register ");line.hex(reg);line.text(" address ");line.hex(address);
  if(!section){line.text(": <redacted: not a complete trusted executable window>\n");line.send(fd,out);continue;}
  const uint8_t* base=guestBase.load(std::memory_order_acquire);
  if(!base){line.text(": <redacted: current code unavailable>\n");line.send(fd,out);continue;}
  // The loader publishes the permanent 4 GiB guest reservation after all section
  // copies. The null guard is below every allowed section. Runtime write-watch
  // changes preserve read permission; no guest range is subsequently unmapped.
  // Volatile loads sample only a complete allowlisted window, never data/heap.
  // Sampling is bounded but not atomic with guest writes. Never emit sampled
  // bytes (or their hash): only an entirely matching immutable baseline is shown.
  const volatile uint8_t* current=base+address;bool matches=true;
  for(unsigned i=0;i<128;++i)if(current[i]!=section->bytes[address-section->address+i])matches=false;
  if(!matches){line.text(": <redacted: modified code>\n");line.send(fd,out);continue;}
  line.text(": 128 current-verified baseline code bytes (non-atomic sample)\n");line.send(fd,out);
  for(unsigned offset=0;offset<128;offset+=32){line.text("    ");line.hex(address+offset);line.text(": ");for(unsigned i=0;i<32;++i)line.byte(section->bytes[address-section->address+offset+i]);line.text("\n");line.send(fd,out);}
 }
 char text[summaryCapacity];unsigned before=revision.load(std::memory_order_seq_cst),size=summarySize.load(std::memory_order_seq_cst);
 if(!(before&1) && size<summaryCapacity){for(unsigned i=0;i<size;++i)text[i]=summary[i].load(std::memory_order_seq_cst);if(before==revision.load(std::memory_order_seq_cst)){out(fd,text,size);return;}}
 line.text("guest memory summary: update interrupted\n");line.send(fd,out);
}
}
