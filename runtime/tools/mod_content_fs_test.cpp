#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
// Exercise the actual HLE filesystem with synthetic files and a small guest-memory window.
#include "runtime.h"
#include "savestate.h"
#include "mods/content.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
// hle/fs.cpp reports opened language packs to the right-to-left text support (not under test)
namespace rtl_text { void language_pack_opened(const std::string&) {} }
namespace game_layouts { void note_open(const std::string&, const std::string&) {} }
namespace fs=std::filesystem;
namespace {std::map<std::string,PpcFunc>& functions(){static std::map<std::string,PpcFunc> f;return f;}constexpr uint32_t base=0x10000000;}
HleReg::HleReg(const char* lib,const char* name,PpcFunc fn){functions()[std::string(lib)+":"+name]=fn;}
bool g_trace_hle=false;
void log_msg(const char*,...){}
namespace config {std::string game_dir,save_dir;}
namespace mem {
std::string read_cstr(uint32_t ea){return reinterpret_cast<const char*>(ptr(ea));}
void write_cstr(uint32_t ea,const std::string& s,uint32_t max){if(max){auto n=std::min<size_t>(max-1,s.size());memcpy(ptr(ea),s.data(),n);ptr(ea)[n]=0;}}
}
namespace threads {void block_begin(){}void block_end(){}}
namespace wwatch {void host_write_begin(uint32_t,uint32_t){}void host_write_end(uint32_t,uint32_t){}}
void fs_ss_save(ss::Writer&);void fs_ss_load(ss::Reader&);
int32_t call(const char* name,std::initializer_list<uint32_t> args){Cpu c{};size_t i=3;for(auto a:args)c.r[i++]=a;functions().at(name)(&c);return int32_t(c.r[3]);}
uint32_t guest_open(const std::string& path,const std::string& mode="rb"){
 mem::write_cstr(base,path,1024);mem::write_cstr(base+1024,mode,32);
 assert(call("coreinit:FSOpenFile",{0,0,base,base+1024,base+1100,0})==0);return ld32(base+1100);
}
std::string guest_read(uint32_t handle,uint32_t count){auto n=call("coreinit:FSReadFile",{0,0,base+8192,1,count,handle,0});assert(n>=0);return {reinterpret_cast<char*>(mem::ptr(base+8192)),size_t(n)};}
void guest_close(uint32_t handle){assert(call("coreinit:FSCloseFile",{0,0,handle})==0);}
std::string text(const fs::path& p){std::ifstream in(p);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){
 assert(argc==2);bool on=std::string(argv[1])=="on";assert(on||std::string(argv[1])=="off");
 auto address=mem::ptr(base);constexpr size_t memory_size=65536;
#ifdef _WIN32
 auto memory=VirtualAlloc(address,memory_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
 auto memory=mmap(address,memory_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
#endif
 assert(memory==address);
 auto root=fs::temp_directory_path()/("wwhd-fs-content-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 config::game_dir=(root/"game").string();config::save_dir=(root/"save").string();
 auto original=root/"game"/"content"/"Common"/"model.bin",replacement=root/"mod"/"Common"/"model.bin";
 fs::create_directories(original.parent_path());fs::create_directories(replacement.parent_path());fs::create_directories(root/"game"/"code");fs::create_directories(root/"game"/"meta");fs::create_directories(root/"save"/"user");
 std::ofstream(original)<<"original";std::ofstream(replacement)<<"replacement bytes";
 std::ofstream(original.parent_path()/"other.bin")<<"fallback";std::ofstream(root/"game"/"code"/"model.bin")<<"code";std::ofstream(root/"game"/"meta"/"model.bin")<<"meta";std::ofstream(root/"save"/"user"/"model.bin")<<"save";
 // Two world-only packages may both retain empty source-control markers.
 for(auto name:{"world-a","world-b"}){auto folder=root/name;fs::create_directories(folder);std::ofstream(folder/".gitkeep");assert(mods::content::index(folder,true).empty());}
 std::ofstream(root/"world-a"/"own.bin")<<"owned";auto world_files=mods::content::index(root/"world-a",true);assert(world_files.size()==1&&world_files.contains("own.bin"));
 if(on)mods::content::activate(mods::content::index(root/"mod"));
 // guest module startup activates the combined set a second time (issue #109: that used to abort the start)
 if(on)mods::content::activate(mods::content::index(root/"mod"));
 auto expected=on?"replacement bytes":"original";
 auto h=guest_open("/vol/content/common/MODEL.bin");assert(guest_read(h,64)==expected);guest_close(h);
 h=guest_open("Common/model.bin");assert(guest_read(h,64)==expected);guest_close(h);
 h=guest_open("/vol/content/Common/other.bin");assert(guest_read(h,64)=="fallback");guest_close(h);
 for(auto volume:{"code","meta"}){h=guest_open(std::string("/vol/")+volume+"/model.bin");assert(guest_read(h,64)==volume);guest_close(h);}
 h=guest_open("/vol/save/user/model.bin");assert(guest_read(h,64)=="save");guest_close(h);
 // Path and handle stat sizes must both report the file actually read.
 mem::write_cstr(base,"/vol/content/Common/model.bin",1024);assert(call("coreinit:FSGetStat",{0,0,base,base+2048,0})==0);assert(ld32(base+2048+0x10)==strlen(expected));
 h=guest_open("/vol/content/Common/model.bin");assert(call("coreinit:FSGetStatFile",{0,0,h,base+2048,0})==0);assert(ld32(base+2048+0x10)==strlen(expected));
 assert(guest_read(h,3)==std::string(expected).substr(0,3));ss::Writer snapshot;fs_ss_save(snapshot);
 assert(guest_read(h,64)==std::string(expected).substr(3));ss::Reader reader(snapshot.b.data(),snapshot.b.size());fs_ss_load(reader);assert(reader.ok&&reader.at_end());assert(guest_read(h,64)==std::string(expected).substr(3));guest_close(h);
 // Update/write opens bypass replacement files, including savestate reopens.
 h=guest_open("/vol/content/Common/model.bin","r+b");assert(guest_read(h,3)=="ori");ss::Writer update;fs_ss_save(update);ss::Reader resume(update.b.data(),update.b.size());fs_ss_load(resume);assert(guest_read(h,64)=="ginal");guest_close(h);
 h=guest_open("/vol/save/user/model.bin","wb");memcpy(mem::ptr(base+8192),"safe",4);assert(call("coreinit:FSWriteFile",{0,0,base+8192,1,4,h,0})==4);guest_close(h);assert(text(root/"save"/"user"/"model.bin")=="safe");
 // Directory enumeration retains original names but must describe replacement sizes.
 mem::write_cstr(base,"/vol/content/Common",1024);assert(call("coreinit:FSOpenDir",{0,0,base,base+1100,0})==0);auto dir=ld32(base+1100);bool found=false;
 while(call("coreinit:FSReadDir",{0,0,dir,base+2048,0})==0){if(mem::read_cstr(base+2048+0x64)=="model.bin"){found=true;assert(ld32(base+2048+0x10)==strlen(expected));}}
 assert(found);assert(call("coreinit:FSCloseDir",{0,0,dir})==0);
 assert(text(original)=="original"&&text(replacement)=="replacement bytes");fs::remove_all(root);
#ifdef _WIN32
 VirtualFree(memory,0,MEM_RELEASE);
#else
 munmap(memory,memory_size);
#endif
 std::cout<<"Guest FS "<<(on?"enabled":"disabled")<<": open/read/stat/enumerate/write/savestate passed\n";
}
