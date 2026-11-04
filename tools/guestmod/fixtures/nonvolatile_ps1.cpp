#undef NDEBUG
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
#include "guest_module_call.h"
static void candidate(Cpu *c, PpcFunc f) { guestmods::invoke_module(c, f); }
static PpcFunc callback;
static int depth, calls;
static bool fail;
static bool nested, guarded;
static void native_call(Cpu *c) {
  assert(c->pc == 0x02000010);
  calls++;
  st32(0x20000040, ld32(0x20000040) + 1);
  if (fail)
    throw 123;
  if (nested && !depth) {
    depth++;
    if (guarded)
      candidate(c, callback);
    else
      callback(c);
    depth--;
  }
  for (unsigned i = 0; i < 14; i++) {
    c->f[i].ps0 = -100 - (int)i;
    c->f[i].ps1 = -200 - (int)i;
  }
}
int main(int argc, char **argv) {
  assert(argc == 2);
#ifdef _WIN32
  auto lib = LoadLibraryA(argv[1]);
  assert(lib);
  auto init = (WWHDGuestInitV1)GetProcAddress(lib, WWHD_GUEST_INIT_SYMBOL);
#else
  auto lib = dlopen(argv[1], RTLD_NOW);
  assert(lib);
  auto init = (WWHDGuestInitV1)dlsym(lib, WWHD_GUEST_INIT_SYMBOL);
#endif
  assert(init);
  volatile int preempt[3] = {0};
  WWHDGuestHostV1 h = {};
  h.size = sizeof(h);
  h.abi_version = WWHD_GUEST_ABI_VERSION;
  h.core_preempt = preempt;
  h.dispatch = native_call;
  auto *m = init(&h);
  assert(m && m->hook_count == 1 && m->func_count >= 1);
  map_memory(0x20000000, 0x10000);
  map_memory(m->mem_base, 0x100000);
  memcpy(ppc_ptr(m->mem_base), m->image, m->image_size);
  callback = nullptr;
  for (unsigned i = 0; i < m->func_count; i++)
    if (m->funcs[i].addr == m->hooks[0].func)
      callback = m->funcs[i].fn;
  assert(callback);
  for (unsigned entry = 0; entry < 2; entry++)
    for (unsigned align = 0; align < 2; align++)
      for (unsigned nest = 0; nest < 2; nest++)
        for (unsigned guard = 0; guard < 2; guard++) {
          Cpu c = {};
          c.r[1] = 0x20008000 + 8 * align;
          unsigned long long before[18];
          for (unsigned i = 14; i < 32; i++) {
            c.f[i].ps0 = 100 + i;
            before[i - 14] = 0x3FF0000000000000ull + i;
            memcpy(&c.f[i].ps1, &before[i - 14], 8);
          }
          nested = nest;
          guarded = guard;
          calls = 0;
          st32(0x20000040, 0);
          st32(0x20001000, 0x12345678);
          st32(0x2000F000, 0x87654321);
          auto fn = entry ? callback : m->hooks[0].fn;
          if (guard)
            candidate(&c, fn);
          else
            fn(&c);
          assert(c.r[1] == 0x20008000 + 8 * align);
          assert(c.f[1].ps0 == 171);
          assert(calls == (nest ? 2 : 1));
          assert(ld32(0x20000040) == unsigned(calls));
          assert(ld32(0x20001000) == 0x12345678);
          assert(ld32(0x2000F000) == 0x87654321);
          unsigned changed = 0;
          for (unsigned i = 14; i < 32; i++) {
            assert(c.f[i].ps0 == 100 + i);
            unsigned long long bits;
            memcpy(&bits, &c.f[i].ps1, 8);
            changed += bits != before[i - 14];
          }
          assert(guard ? changed == 0 : changed > 0);
          printf("entry=%s spmod16=%u nested=%u guard=%u changed_ps1=%u "
                 "result=171 calls=%d\n",
                 entry ? "module-function-callback" : "replacement", align * 8,
                 nest, guard, changed, calls);
        }
  // Unwind through the actual module and boundary, preserving raw NaN payloads
  // too.
  Cpu c = {};
  c.r[1] = 0x20008000;
  uint64_t expected[18];
  for (unsigned i = 0; i < 18; i++) {
    expected[i] = 0x7ff8000000000001ull + i;
    memcpy(&c.f[14 + i].ps1, expected + i, 8);
  }
  fail = true;
  st32(0x20000040, 0);
  bool caught = false;
  try {
    candidate(&c, m->hooks[0].fn);
  } catch (int value) {
    assert(value == 123);
    caught = true;
  }
  assert(caught);
  assert(ld32(0x20000040) == 1);
  for (unsigned i = 0; i < 18; i++)
    assert(!memcmp(&c.f[14 + i].ps1, expected + i, 8));
  puts("exception-restoration=pass");
  return 0;
}
