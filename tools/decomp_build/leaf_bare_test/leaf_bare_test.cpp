// leaf_bare_test: gabi.h GABI_LEAF_BARE (the port's WWHD_DECOMP_FAST_LEAF_BARE) with the real gabi.h and glue.cpp:
//  1. which bodies are bare (glue_bare_of): only a leaf (bit 20) marked bare by native_rules.py --bare (bit 22) whose
//     result and parameters take a native entry and that has no char / void / function pointer parameter;
//  2. a bare body keeps no glue: called nested (depth > 0) with caller_ok 0, which refuses every other body, it still
//     runs its own code and leaves depth, current and caller_ok as they were;
//  3. (run.sh) the bare body makes no gabi_ts_pass call, the non-bare leaf does (objdump);
//  4. GABI_BARE_ENTRY (run.sh's second build): a bare leaf with bit 23 is bare whatever its pointer parameters
//     (entry_body: no gabi_ts_pass call), without it entry_body keeps the glue.
#include <cstdint>
#define GABI_NATIVE_TABLE 1
namespace gabi_native {
// address, slot, flags: 1 callee=native, 4 listed, 8 register-exact, 2 int registers, result r3, 20 leaf, 22 bare
inline constexpr uint32_t kTab[][3] = {
    {0x02000000u, 0u, 1u | 4u | 8u | (2u << 8) | (1u << 16) | (1u << 20) | (1u << 22)}, // bare leaf
    {0x02000010u, 1u, 1u | 4u | 8u | (2u << 8) | (1u << 16) | (1u << 20)},              // leaf, its unit not bare
    {0x02000020u, 2u, 1u | 4u | 8u | (2u << 8) | (1u << 16) | (1u << 22)},              // bit 22 without leaf
    {0x02000030u, 3u, 1u | 4u | 8u | (2u << 8) | (1u << 16) | (1u << 20) | (1u << 22) | (1u << 23)}, // bare, entry only
    {0x02000040u, 4u, 1u | 4u | 8u | (2u << 8) | (1u << 16) | (1u << 20) | (1u << 23)},              // bit 23, not bare
};
}
#define GABI_TLS_OWNER 1
#define DECOMP_UNIT "leaf_bare_test"
#include "../glue.cpp"
#include <cstdio>
extern "C" void decomp_register(const char*, uint32_t, const char*, void (*)(Cpu*), void (*)(Cpu*, void (*)(Cpu*))) {}
extern "C" void decomp_register_native(const char*, u32, void*, u32, u32) {}
extern "C" void ppc_trap(Cpu*, uint32_t) { __builtin_trap(); }
/* the runtime's data the non-bare leaf_body's glue reads (never run here; its guest-call functions stay unresolved) */
extern "C" const GabiPtrEntry* g_decomp_call_tab = nullptr;
extern "C" uint8_t* g_decomp_native_ok = nullptr;
extern "C" u32 g_decomp_ptr_mask = 0;
extern "C" uint8_t g_decomp_leaf_off = 0;
extern "C" uint8_t g_decomp_nocheck_off = 0;

constexpr u32 BARE = gabi::native_info(0x02000000u).flags, LEAF = gabi::native_info(0x02000010u).flags,
              NOLEAF = gabi::native_info(0x02000020u).flags;
constexpr u32 ENTRY = gabi::native_info(0x02000030u).flags, ENTRY_NB = gabi::native_info(0x02000040u).flags;
struct S { gabi::be<u32> v; };
template <u32 F, class R, bool OK, class... A> constexpr bool bare() {
    return decltype(gabi::glue_bare_of<F, R, OK>(std::declval<const A&>()...))::value;
}
// 1. the rule
static_assert(bare<BARE, u32, true, S*, u32>(), "a bare leaf with typed pointers and integers is bare");
static_assert(!bare<LEAF, u32, true, S*, u32>(), "a leaf without bit 22 (caller=guest / unbased unit) keeps the glue");
static_assert(!bare<NOLEAF, u32, true, S*, u32>(), "bit 22 without the leaf bit keeps the glue");
static_assert(!bare<BARE, u32, true, void*, u32>(), "a void pointer parameter keeps the glue");
static_assert(!bare<BARE, u32, true, const char*>(), "a char pointer parameter keeps the glue");
static_assert(!bare<BARE, u32, true, unsigned char*>(), "an unsigned char pointer parameter keeps the glue");
static_assert(!bare<BARE, u32, true, void (*)(int)>(), "a function pointer parameter keeps the glue");
static_assert(!bare<BARE, u32, false, S*>(), "returns not rewritten (RET_OK false) keep the glue");
static_assert(!bare<BARE, u32&, true, S*>(), "a reference result keeps the glue");
static_assert(bare<BARE, void, true>(), "a void leaf without parameters is bare");
// GABI_BARE_ENTRY (run.sh's second build): bit 23 lifts the parameter rule only, and only with bit 22 and 20
#ifdef GABI_BARE_ENTRY
static_assert(bare<ENTRY, u32, true, void*, void*>(), "bit 23: a void pointer parameter is bare");
static_assert(bare<ENTRY, u32, true, const char*, void (*)(int)>(), "bit 23: char and function pointers are bare");
static_assert(bare<ENTRY, u32, true, S*, u32>(), "bit 23: typed pointers and integers stay bare");
static_assert(!bare<ENTRY, u32, false, void*>(), "bit 23: returns not rewritten still keep the glue");
static_assert(!bare<ENTRY, u32&, true, void*>(), "bit 23: a reference result still keeps the glue");
static_assert(!bare<ENTRY_NB, u32, true, void*>(), "bit 23 without bit 22 keeps the glue");
static_assert(!bare<BARE, u32, true, void*, u32>(), "bit 22 alone still keeps the glue for a void pointer");
#else
static_assert(!bare<ENTRY, u32, true, void*, void*>(), "without GABI_BARE_ENTRY bit 23 changes nothing");
static_assert(bare<ENTRY, u32, true, S*, u32>(), "without GABI_BARE_ENTRY a bit-23 leaf with typed pointers is bare");
#endif

// 2. behaviour (the bodies as gen.py writes them)
extern "C" [[gnu::noinline]] u32 bare_body(S* s, u32 k) {
    WWHD_GLUE_FUNC_R(0x02000000u, u32, s, k);
    return wwhd_native_scope_.ret<wwhd_ret_t_>((u32)s->v + k);
}
extern "C" [[gnu::noinline]] u32 entry_body(void* s, u32 k) {
    WWHD_GLUE_FUNC_R(0x02000030u, u32, s, k);
    return wwhd_native_scope_.ret<wwhd_ret_t_>((u32) static_cast<S*>(s)->v + k);
}
extern "C" [[gnu::noinline]] u32 leaf_body(S* s, u32 k) {
    WWHD_GLUE_FUNC_R(0x02000010u, u32, s, k);
    return wwhd_native_scope_.ret<wwhd_ret_t_>((u32)s->v + k);
}
int main() {
    int bad = 0;
    gabi::ThreadState& t = *gabi::gabi_ts_pass();
    S* s = gabi::at<S>(0x10000000u);
    t.depth = 3; t.current = 0x02345678u; t.caller_ok = 0;
    if (gabi::Activation::nested() != true) bad++;
    u32 r = bare_body(s, 5);
#ifdef GABI_BARE_ENTRY
    r += entry_body(s, 5); /* (without GABI_BARE_ENTRY it keeps the glue: refused here, its guest call unresolved) */
#endif
    if (t.depth != 3 || t.current != 0x02345678u || t.caller_ok != 0) { std::printf("bare body changed the call state\n"); bad++; }
    (void)r;
    std::printf("leaf_bare_test: rule asserts compiled, bare body call state %s\n", bad ? "CHANGED" : "unchanged");
    std::printf("leaf_bare_test: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
