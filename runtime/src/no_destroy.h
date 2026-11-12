// SPDX-License-Identifier: MPL-2.0
// A static object that is never destroyed: for objects that detached host threads (scheduler tick, alarm thread,
// guest threads waiting for their core) may still use when the process exits through std::exit or a return from
// main. glibc's pthread_cond_destroy waits until every thread waiting on the condition has left its wait; a waiter
// with no timeout keeps the exiting thread there forever, and one in a timed wait loop (the tick's 100 ms wait, a
// core waiter's 2 s wait) is back in the wait before the exiting thread runs again, so exit hangs (seen on Linux CI).
// The storage is constructed like a plain static (constant-initialised where T allows it) and lives until the
// process ends; the OS frees it.
#pragma once

template <class T> union NoDestroy {
    T value;
    constexpr NoDestroy() : value() {}
    ~NoDestroy() {}  // value is not destroyed
    NoDestroy(const NoDestroy&) = delete;
    NoDestroy& operator=(const NoDestroy&) = delete;
};
