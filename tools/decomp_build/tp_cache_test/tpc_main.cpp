// tp-cache churn test: many short threads (joinable and detached, reused thread pointers), signals during lookups,
// a fork; every hit checked against the real thread-local block. Exit 1 on any stale hit.
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <set>
#include <mutex>
#include <vector>
#include <cstdlib>
static int (*check)(); static void (*enter)(); static uintptr_t (*key)(); static int (*used)();
static std::atomic<long> hits{0}, misses{0}, stale{0}, sig_lookups{0}, sig_stale{0};
static std::mutex mu; static std::set<uintptr_t> keys; static long reused = 0;
static void one(long n) {
    for (long i = 0; i < n; i++) { int r = check(); if (r > 0) hits++; else if (r == 0) misses++; else stale++; }
}
static void on_sig(int) { int r = check(); sig_lookups++; if (r < 0) { stale++; sig_stale++; } }
static void* worker(void* a) {
    { std::lock_guard<std::mutex> g(mu); if (!keys.insert(key()).second) reused++; }
    enter();
    one((long)(intptr_t)a);
    return nullptr;
}
int main(int argc, char** argv) {
    void* h = dlopen(argc > 1 ? argv[1] : "./libtpc.so", RTLD_NOW);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 2; }
    check = (int (*)())dlsym(h, "tpc_check"); key = (uintptr_t (*)())dlsym(h, "tpc_key"); used = (int (*)())dlsym(h, "tpc_used"); enter = (void (*)())dlsym(h, "tpc_enter"); enter();
    signal(SIGUSR1, on_sig);
    const int rounds = argc > 2 ? atoi(argv[2]) : 2000, width = 24;
    long created = 0;
    for (int r = 0; r < rounds; r++) {
        std::vector<pthread_t> t(width);
        for (int i = 0; i < width; i++) {
            pthread_attr_t at; pthread_attr_init(&at);
            if (i % 4 == 3) pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
            pthread_create(&t[i], &at, worker, (void*)(intptr_t)(200 + (i * 37 + r) % 400)); created++;
            if (getenv("NOSIG") == nullptr && i % 4 != 3 && (r + i) % 5 == 0) pthread_kill(t[i], SIGUSR1); // a signal while it may be claiming
            pthread_attr_destroy(&at);
        }
        for (int i = 0; i < width; i++) if (i % 4 != 3) pthread_join(t[i], nullptr);
        one(50); // the main thread keeps its slot all along
    }
    usleep(200000); // detached threads finish
    int child_stale = -1;
    pid_t p = fork();
    if (p == 0) { // child: only this thread; new threads must not match a parent thread's key
        stale = 0; for (int i = 0; i < 200; i++) { pthread_t t; pthread_create(&t, nullptr, worker, (void*)(intptr_t)300); pthread_join(t, nullptr); }
        _exit(stale.load() ? 1 : 0);
    }
    int st = 0; waitpid(p, &st, 0); child_stale = WIFEXITED(st) ? WEXITSTATUS(st) : 99;
    printf("threads %ld, distinct keys %zu, reused keys %ld, hits %ld, misses %ld, signal lookups %ld, stale %ld (in handlers %ld), "
           "slots in use at end %d, fork child %s\n", created, keys.size(), reused, hits.load(), misses.load(),
           sig_lookups.load(), stale.load(), sig_stale.load(), used(), child_stale == 0 ? "clean" : "STALE");
    ((void (*)())dlsym(h, "tpc_counts"))();
    return stale.load() || child_stale ? 1 : 0;
}
