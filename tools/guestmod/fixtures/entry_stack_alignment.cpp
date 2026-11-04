#undef NDEBUG
#include "guest_module_call.h"
#include "wwhd_guest_abi.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif
using guestmods::ModuleStackBounds;
using guestmods::ModuleStackEntry;
static PpcFunc entry;
static ModuleStackBounds bounds{0x20000000, 0x20100000};
static unsigned calls, diagnostics, depth;
static bool nested, fail, expect_aligned, observer;
static uint32_t expected_frame, expected_parent, expected_lr;
static void diagnostic(ModuleStackEntry) noexcept { ++diagnostics; }
static void map_memory(uint32_t address, size_t size) {
#ifdef _WIN32
  assert(VirtualAlloc(ppc_ptr(address), size, MEM_RESERVE | MEM_COMMIT,
                      PAGE_READWRITE) == ppc_ptr(address));
#else
  assert(mmap(ppc_ptr(address), size, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1,
              0) == ppc_ptr(address));
#endif
}
static void args(Cpu &c, uint32_t sp, uint32_t out, uint32_t escaped) {
  c.r[1] = sp;
  c.r[3] = out;
  for (unsigned i = 4; i <= 10; i++)
    c.r[i] = i;
  st32(sp + 8, 0x999);
  st32(sp + 12, 0xAAA);
  st32(sp + 16, escaped);
  st32(out, 0);
  st32(escaped, 7);
}
static void native_call(Cpu *c) {
  assert(c->pc == 0x02000010);
  ++calls;
  const uint32_t local = c->r[3], out = c->r[4], escaped = c->r[5];
  assert(ld32(local) == 0x999 && ld32(local + 4) == 0xAAA &&
         ld32(local + 8) == 7 && ld32(local + 12) == 49);
  assert(c->r[6] == 0x999 && c->r[7] == 0xAAA);
  const bool aligned = !(local & 15);
  assert(aligned == expect_aligned);
  if (aligned) {
    // The relocated caller retains its real parent, not a fictitious old-SP
    // frame.
    assert(ld32(c->r[1]) == expected_frame);
    assert(ld32(expected_frame) == expected_parent);
    assert(ld32(expected_frame + 4) == expected_lr);
    assert(ld32(expected_parent + 4) == 0x0200BEEF);
  }
  st32(out, 0x13579BDF);
  st32(escaped, 0x2468ACE0);
  if (fail) {
    for (unsigned i = 13; i < 32; i++)
      c->r[i] = 0;
    for (unsigned i = 14; i < 32; i++) {
      c->f[i].ps0 = -1;
      c->f[i].ps1 = -2;
    }
    memset(c->cr + 8, 0, 12);
    c->lr = 0;
    c->ctr = 0;
    throw 123;
  }
  if (nested && !depth) {
    Cpu saved = *c;
    const auto saved_frame = expected_frame, saved_parent = expected_parent,
               saved_lr = expected_lr;
    const uint32_t native_sp = c->r[1], call_sp = native_sp - 0x18;
    st32(call_sp, native_sp);
    st32(native_sp + 4, 0x0200BEEF);
    args(*c, call_sp, 0x20070000, 0x20070004);
    c->lr = 0x0200CAFE;
    expected_frame = (call_sp - 0x18) & ~uint32_t(15);
    expected_parent = native_sp;
    expected_lr = c->lr;
    ++depth;
    auto result = guestmods::invoke_module(c, entry, bounds, diagnostic);
    --depth;
    assert(result == ModuleStackEntry::Shadowed && c->r[1] == call_sp &&
           c->lr == 0x0200CAFE);
    assert((observer || c->r[3] == 1) && ld32(0x20070000) == 0x13579BDF + 49 &&
           ld32(0x20070004) == 0x2468ACE0);
    *c = saved;
    expected_frame = saved_frame;
    expected_parent = saved_parent;
    expected_lr = saved_lr;
  }
  c->f[1].ps0 = 123.5;
  c->r[3] = aligned ? 1 : 0;
}
int main(int argc, char **argv) {
  assert(argc == 2);
#ifdef _WIN32
  auto library = LoadLibraryA(argv[1]);
  assert(library);
  auto init = (WWHDGuestInitV1)GetProcAddress(library, WWHD_GUEST_INIT_SYMBOL);
#else
  auto library = dlopen(argv[1], RTLD_NOW);
  assert(library);
  auto init = (WWHDGuestInitV1)dlsym(library, WWHD_GUEST_INIT_SYMBOL);
#endif
  assert(init);
  volatile int preempt[3] = {};
  WWHDGuestHostV1 host = {};
  host.size = sizeof(host);
  host.abi_version = WWHD_GUEST_ABI_VERSION;
  host.core_preempt = preempt;
  host.dispatch = native_call;
  auto *module = init(&host);
  assert(module && module->hook_count == 3);
  map_memory(0x20000000, 0x100000);
  map_memory(module->mem_base, 0x100000);
  memcpy(ppc_ptr(module->mem_base), module->image, module->image_size);
  PpcFunc replacement = nullptr, callback = nullptr, entry_hook = nullptr,
          return_hook = nullptr;
  uint32_t replacement_address = 0;
  for (unsigned i = 0; i < module->hook_count; i++) {
    const auto &hook = module->hooks[i];
    if (hook.kind == WWHD_GUEST_REPLACE) {
      replacement = hook.fn;
      replacement_address = hook.func;
    } else if (hook.kind == WWHD_GUEST_HOOK_ENTRY)
      entry_hook = hook.fn;
    else if (hook.kind == WWHD_GUEST_HOOK_RETURN)
      return_hook = hook.fn;
  }
  for (unsigned i = 0; i < module->func_count; i++)
    if (module->funcs[i].addr == replacement_address)
      callback = module->funcs[i].fn;
  assert(replacement && callback && entry_hook && return_hook);
  for (unsigned path = 0; path < 4; path++)
    for (unsigned alignment = 0; alignment < 2; alignment++)
      for (unsigned scenario = 0; scenario < 6; scenario++) {
        // raw, protected, nested, missing receipt, invalid backchain, low
        // headroom.
        entry = path == 0   ? replacement
                : path == 1 ? callback
                : path == 2 ? entry_hook
                            : return_hook;
        observer = path >= 2;
        Cpu c = {};
        const uint32_t sp = 0x20030000 + alignment * 8, parent = sp + 0x200;
        memset(ppc_ptr(sp), 0xA5, 0x400);
        st32(sp, parent);
        st32(parent + 4, 0x0200BEEF);
        args(c, sp, sp + 0x100, sp + 0x104);
        c.lr = 0x0200C0DE;
        c.ctr = 0x12345678;
        for (unsigned i = 13; i < 32; i++)
          c.r[i] = 1000 + i;
        for (unsigned i = 14; i < 32; i++) {
          c.f[i].ps0 = 100 + i;
          c.f[i].ps1 = 1000 + i;
        }
        uint8_t before[0x200];
        memcpy(before, ppc_ptr(sp), sizeof(before));
        ModuleStackBounds selected = bounds;
        if (scenario == 3)
          selected = {};
        if (scenario == 4 && alignment)
          st32(sp, parent + 0x100000);
        if (scenario == 5)
          selected.bottom = sp - 0x4000;
        nested = scenario == 2;
        fail = false;
        calls = diagnostics = depth = 0;
        expect_aligned = alignment == 0 || (scenario == 1 || scenario == 2);
        expected_frame = alignment ? ((sp - 0x200) & ~uint32_t(15)) : sp;
        expected_parent = parent;
        expected_lr = c.lr;
        printf("path=%u alignment=%u scenario=%u\n", path, alignment, scenario);
        fflush(stdout);
        const Cpu observer_saved = c;
        ModuleStackEntry result = ModuleStackEntry::Aligned;
        if (scenario == 0)
          guestmods::invoke_module(&c, entry);
        else
          result = guestmods::invoke_module(&c, entry, selected, diagnostic);
        // The module's epilogue restores LR; CTR is volatile and stays the
        // module's output, as for any call without the adapter.
        assert(c.r[1] == sp && c.lr == 0x0200C0DE);
        if (!observer)
          assert(c.r[3] == unsigned(expect_aligned));
        assert(c.f[1].ps0 == 123.5);
        assert(calls == (nested ? 2u : 1u));
        assert(ld32(sp + 0x100) == 0x13579BDF + 49 &&
               ld32(sp + 0x104) == 0x2468ACE0);
        for (unsigned i = 0; i < sizeof(before); i++)
          if (!(i >= 4 && i < 8) && !(i >= 0x100 && i < 0x108) &&
              !(scenario == 4 && i < 4))
            assert(ppc_ptr(sp)[i] == before[i]);
        if (scenario && alignment) {
          assert(diagnostics == unsigned(scenario >= 3));
          assert(result == (scenario < 3    ? ModuleStackEntry::Shadowed
                            : scenario == 3 ? ModuleStackEntry::UnknownBounds
                            : scenario == 4
                                ? ModuleStackEntry::InvalidFrame
                                : ModuleStackEntry::InsufficientHeadroom));
        }
        for (unsigned i = 14; i < 32; i++)
          assert(c.f[i].ps0 == 100 + i && c.f[i].ps1 == 1000 + i);
        if (observer) {
          // The runtime's existing ObserverRegisters discards register writes,
          // but deliberately keeps the memory effects checked above.
          c = observer_saved;
          assert(!memcmp(&c, &observer_saved, sizeof(c)));
          assert(ld32(sp + 0x100) == 0x13579BDF + 49 &&
                 ld32(sp + 0x104) == 0x2468ACE0);
        }
      }
  // Host unwinding skips the simulated PPC epilogue; restore control and
  // nonvolatile state.
  entry = replacement;
  observer = false;
  Cpu c = {};
  const uint32_t sp = 0x20030008, parent = sp + 0x200;
  st32(sp, parent);
  st32(parent + 4, 0x0200BEEF);
  args(c, sp, sp + 0x100, sp + 0x104);
  c.lr = 0x0200C0DE;
  c.ctr = 0x11223344;
  for (unsigned i = 13; i < 32; i++)
    c.r[i] = 2000 + i;
  for (unsigned i = 14; i < 32; i++) {
    c.f[i].ps0 = 3000 + i;
    c.f[i].ps1 = 4000 + i;
  }
  memset(c.cr + 8, 1, 12);
  expected_frame = (sp - 0x200) & ~uint32_t(15);
  expected_parent = parent;
  expected_lr = c.lr;
  expect_aligned = true;
  fail = true;
  nested = false;
  bool caught = false;
  try {
    guestmods::invoke_module(&c, entry, bounds, diagnostic);
  } catch (int value) {
    assert(value == 123);
    caught = true;
  }
  assert(caught && c.r[1] == sp && c.lr == 0x0200C0DE && c.ctr == 0x11223344);
  for (unsigned i = 13; i < 32; i++)
    assert(c.r[i] == 2000 + i);
  for (unsigned i = 14; i < 32; i++)
    assert(c.f[i].ps0 == 3000 + i && c.f[i].ps1 == 4000 + i);
  for (unsigned i = 8; i < 20; i++)
    assert(c.cr[i] == 1);
  assert(ld32(sp + 0x100) == 0x13579BDF && ld32(sp + 0x104) == 0x2468ACE0);
  puts("48 flagless entry/alias/backchain/fallback scenarios and exception "
       "restoration passed");
}
