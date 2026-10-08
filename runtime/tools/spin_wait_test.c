/* Recompiled spin waits must see other threads' stores (issue #62).
 *
 * The game busy-waits on spin locks and flags that another core changes. The recompiler turns such a
 * loop into plain guest loads inside a call-free loop over a __restrict Cpu, which an optimizing
 * compiler may load once before the loop: LLVM 16/17 (Apple clang 16 of Xcode 16) compiled the
 * job-queue lock wait of f_027588DC into `b .`, and the game froze on its first frames. The
 * recompiler puts PPC_LOOP() (a compiler barrier, ppc.h) on every loop back-edge; spin_lock() below
 * is the generated code of that wait (02758940-02758964), compiled with the game code's flags.
 * Without the barrier, LLVM 17 hangs here; with it, the wait ends as soon as the other thread
 * releases the lock. */
#include "ppc.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

enum { kLock = 0x10000010u };

__attribute__((noinline)) void spin_lock(Cpu* __restrict c) {
    c->r[31] = kLock;
L_02758940: ;
    c->r[10] = ld32(c->r[31] + 0x00000000u); /* 02758940: 815F0000 */
    cr_set_s(c, 0, (int32_t)c->r[10], 1); /* 02758944: 2C0A0001 */
    if (c->cr[2]) { PPC_LOOP(); goto L_02758940; } /* 02758948: 4182FFF8 */
L_0275894C: ;
    c->r[7] = ppc_lwarx(c, 0u + c->r[31]); /* 0275894C: 7CE0F828 */
    cr_set_s(c, 0, (int32_t)c->r[7], 0); /* 02758950: 2C070000 */
    if (!c->cr[2]) { PPC_LOOP(); goto L_0275894C; } /* 02758954: 4082FFF8 */
    c->r[0] = 0u + 0x00000001u; /* 0275895C: 38000001 */
    ppc_stwcx(c, 0u + c->r[31], c->r[0]); /* 02758960: 7C00F92D */
    if (!c->cr[2]) { PPC_LOOP(); goto L_0275894C; } /* 02758964: 4082FFE8 */
}

static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void* releaser(void* arg) {  // the lock holder on another core
    (void)arg;
    sleep_ms(50);
    __atomic_store_n((uint32_t*)ppc_ptr(kLock), 0u, __ATOMIC_SEQ_CST);
    return NULL;
}

static void* watchdog(void* arg) {
    (void)arg;
    sleep_ms(5000);
    fprintf(stderr, "spin_wait_test: FAIL, the spin wait never saw the lock released (stale load hoisted out of the loop)\n");
    _exit(1);
}

int main(void) {
    uint8_t* page = ppc_ptr(kLock & ~0xFFFFu);
    if (mmap(page, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) != page) {
        perror("spin_wait_test: mmap guest page");
        return 1;
    }
    st32(kLock, 1);  // held by the other thread
    pthread_t r, w;
    pthread_create(&w, NULL, watchdog, NULL);
    pthread_create(&r, NULL, releaser, NULL);
    static Cpu c;
    spin_lock(&c);
    pthread_join(r, NULL);
    if (ld32(kLock) != 1) {
        fprintf(stderr, "spin_wait_test: FAIL, lock not taken\n");
        return 1;
    }
    printf("spin_wait_test: ok\n");
    return 0;
}
