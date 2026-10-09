// Exercise the actual hook runner with synthetic game bodies and a port wrapper.
#include "mods/guest_mods.cpp"
#include <cassert>
#include <vector>
#ifndef _WIN32
#include <sys/mman.h>
#endif

static std::vector<int> calls;
extern "C" void guest_hooks_body(Cpu* c);
extern "C" void guest_hooks_record_body() { calls.push_back(3); }
static void port(Cpu* c) {
    calls.push_back(1);
    c->r[3] *= 2; // stand-in for true-60 scaling before game code / replacements
    guest_hooks_body(c);
    calls.push_back(6);
}
static void entry(Cpu* c) { calls.push_back(2); assert(c->r[3] == 10); c->r[3] = 999; }
static void replacement(Cpu* c) {
    calls.push_back(7); assert(c->r[3] == 10);
    guestmods::call_original(c, 0x02000000);
    assert(c->mod_skip == 0);
}
static void ret_a(Cpu* c) { calls.push_back(4); assert(c->r[3] == 10); c->r[3] = 888; }
static void ret_b(Cpu* c) { calls.push_back(5); assert(c->r[3] == 10); c->r[3] = 777; }
extern "C" {
extern const char g_guest_build_name[] = "USA";
extern const RecompEntry g_recomp_funcs[] = {{0x02000000, port}};
extern const unsigned g_recomp_func_count = 1;
static const PpcFunc test_mod_bodies[] = {guest_hooks_body};
static uint8_t test_mod_flags[1]{};
volatile int g_core_preempt[3]{};
int g_ppc_trace = 0;
void ppc_trace_enter(uint32_t) {}
void ppc_dispatch(Cpu*) { std::abort(); }
void ppc_unimplemented(Cpu*, uint32_t, uint32_t) { std::abort(); }
void ppc_trap(Cpu*, uint32_t) { std::abort(); }
uint64_t ppc_timebase() { return 0; }
double ppc_fres(double x) { return x; }
double ppc_frsqrte(double x) { return x; }
void ppc_preempt(Cpu*) {}
}
namespace guestmods::settings {
std::optional<Value> read(const std::string& key) {
    if(key=="input.face_layout")return Value{std::string("labels"),7};
    if(key=="render.true60")return Value{true,8};
    if(key=="display.aspect")return Value{21.0/9,9};
    return {};
}
}
namespace input {PadState read() {return {};}}
namespace true60 {float dt() {return 0.5f;}}
namespace mem { std::string read_cstr(uint32_t) { return "option"; } }
namespace dispatch { void set(uint32_t, PpcFunc) {} }
namespace mods::packages {
static bool in_startup_callback=false, inspected=false;
std::string directory() { assert(!in_startup_callback); return {}; }
void start_guests(const GuestInspect& inspect, const GuestLoad&) {
    // Real start_guests holds the manager mutex while invoking callbacks. Its
    // directory accessor cannot be called recursively from the build bridge.
    in_startup_callback=true;
    try { inspect(GuestPackage{}); assert(false); }
    catch(const std::runtime_error& e) {
        assert(std::string(e.what()).find("build tools are unavailable")!=std::string::npos);
        inspected=true;
    }
    in_startup_callback=false;
}
}
void log_msg(const char*, ...) {}
[[noreturn]] void fatal(const char*, ...) { std::abort(); }
int main() {
    assert(!guestmods::hooks_built());
    ppc_mod_register(1, test_mod_flags, test_mod_bodies);
    assert(guestmods::hooks_built());
    const char* missing="__wwhd_nonexistent_guest_build_config_for_test__.json";
    assert(!std::filesystem::exists(missing));
#ifdef _WIN32
    _putenv_s("WWHD_GUEST_BUILD_CONFIG",missing);
#else
    setenv("WWHD_GUEST_BUILD_CONFIG",missing,1);
#endif
    guestmods::init();
    assert(mods::packages::inspected);
    Cpu c{};
    c.r[3] = 5; port(&c);
    assert((calls == std::vector<int>{1, 3, 6})); assert(c.r[3] == 11);
    auto& chain = guestmods::g_chains[0x02000000];
    chain.addr = 0x02000000; chain.ordinal = 0;
    chain.entry = {entry}; chain.ret = {ret_b, ret_a};
    g_mod_hook_flags[0] = 1;
    calls.clear(); c.r[3] = 5; port(&c);
    assert((calls == std::vector<int>{1, 2, 3, 5, 4, 6})); assert(c.r[3] == 11);
    chain.replace = replacement;
    calls.clear(); c.r[3] = 5; port(&c);
    assert((calls == std::vector<int>{1, 2, 7, 3, 5, 4, 6})); assert(c.r[3] == 11);
    // ORIGINAL bypasses both mods and the port wrapper, and consumes the skip token.
    calls.clear(); c.r[3] = 20; guestmods::call_original(&c, 0x02000000);
    assert((calls == std::vector<int>{3})); assert(c.r[3] == 21); assert(c.mod_skip == 0);
    // Map only synthetic mod data for string/input/heap service checks.
    constexpr uint32_t data_base=0x7F000000;
#ifdef _WIN32
    void* data=VirtualAlloc(mem::ptr(data_base),0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    void* data=mmap(mem::ptr(data_base),0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#endif
    assert(data==mem::ptr(data_base));
    // Typed services select the owning mod by its translated import callsite.
    WWHDGuestModuleV1 module{};module.mem_base=0x7F000000;module.mem_size=4096;
    guestmods::Loaded mod;mod.id="first";mod.m=&module;mod.options["option"]=42;
    guestmods::g_loaded.push_back(std::move(mod));c.pc=0x7F000100;c.r[3]=1;c.r[4]=9;
    guestmods::svc_config_int(&c);assert(c.r[3]==42);
    guestmods::g_loaded[0].options["option"]=true;c.r[3]=1;
    guestmods::svc_config_bool(&c);assert(c.r[3]==1);
    c.r[3]=1;c.r[4]=9;guestmods::svc_config_int(&c);assert(c.r[3]==9); // wrong type uses fallback
    guestmods::g_loaded[0].options["option"]=2.5;c.r[3]=1;c.f[1].ps0=8;
    guestmods::svc_config_float(&c);assert(c.f[1].ps0==2.5);
    c.r[3]=1;c.r[4]=9;guestmods::svc_config_int(&c);assert(c.r[3]==9); // non-integral numeric option
    guestmods::svc_logic_dt(&c);assert(c.f[1].ps0==1.0/60.0);
    guestmods::frame(0x100000002ull);guestmods::svc_logic_step(&c);assert(c.r[3]==1&&c.r[4]==2);
    guestmods::g_loaded[0].region_size=0x10000;
    auto read_setting=[&](const char* key,uint32_t type,uint32_t destination,uint32_t capacity) {
        std::strcpy(reinterpret_cast<char*>(mem::ptr(data_base+0x400)),key);
        c.r[3]=data_base+0x400;c.r[4]=type;c.r[5]=destination;c.r[6]=capacity;
        guestmods::svc_setting_get(&c);return c.r[3];
    };
    auto target=data_base+0x500;
    assert(read_setting("input.face_layout",1,target,32)==7);
    assert(std::string(reinterpret_cast<char*>(mem::ptr(target)))=="labels");
    assert(read_setting("input.face_layout",1,0,0)==7);
    assert(read_setting("input.face_layout",2,target,32)==0);
    assert(read_setting("unknown",1,target,32)==0);
    assert(read_setting("input.face_layout",1,target,2)==0&&ld8(target)=='l');
    assert(read_setting("input.face_layout",1,data_base+0xFFFF,32)==0);
    assert(read_setting("render.true60",2,target,4)==4&&ld32(target)==1);
    assert(read_setting("display.aspect",4,target,8)==8&&ldf64(target)==21.0/9);
    std::strcpy(reinterpret_cast<char*>(mem::ptr(data_base+0x400)),"input.face_layout");
    c.r[3]=data_base+0x400;guestmods::svc_setting_changed(&c);assert(c.r[3]==0&&c.r[4]==7);
    WWHDGuestModuleV1 second{};second.mem_base=0x7F010000;second.mem_size=4096;
    guestmods::Loaded another;another.id="second";another.m=&second;another.options["option"]=84;
    guestmods::g_loaded.push_back(std::move(another));c.pc=0x7F010100;c.r[3]=1;
    guestmods::svc_config_int(&c);assert(c.r[3]==84);
    c.pc=0x7F000100;c.r[3]=1;c.f[1].ps0=9;
    guestmods::g_loaded[0].options["option"]="text";
    guestmods::svc_config_float(&c);assert(c.f[1].ps0==9);
    c.r[3]=1;c.r[4]=data_base+0x200;c.r[5]=3;
    guestmods::svc_config_string(&c);assert(c.r[3]==2);assert(std::string(reinterpret_cast<char*>(mem::ptr(data_base+0x200)))=="te");
    guestmods::g_loaded[0].heap=std::make_unique<guestmods::Heap>(mem::ptr(data_base+0x1000),data_base+0x1000,4096);
    guestmods::g_loaded[0].heap->initialize();c.r[3]=32;guestmods::svc_malloc(&c);
    assert(c.r[3]==data_base+0x1010);guestmods::svc_free(&c);
    c.r[3]=data_base+0x300;guestmods::svc_input(&c);assert(ld32(data_base+0x300)==0);
#ifdef _WIN32
    VirtualFree(data,0,MEM_RELEASE);
#else
    munmap(data,0x20000);
#endif
    std::puts("guest hook ordering and original passed");
}
