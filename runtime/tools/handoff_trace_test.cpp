#include "gfx/vulkan/handoff_trace.h"
#include <cassert>
#include <cstdlib>
#include <thread>
#include <vector>
#include <fstream>
#include <string>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif
using namespace gfxvk::handoff_trace;
int main(int argc, char**) {
    if (argc > 1) {
#ifdef _WIN32
        assert(_putenv_s("WWHD_VK_HANDOFF_TRACE", "") == 0);
#else
        assert(unsetenv("WWHD_VK_HANDOFF_TRACE") == 0);
#endif
        assert(!enabled());
        emit(Lane::Producer, Kind::PublishEnter);
        set_producer_frame(77);
        assert(producer_frame() == 0 && flush());
        return 0;
    }
    Buffer<4> bounds;
    for (unsigned i = 0; i < 4; ++i)
        assert(bounds.append({i, i + 1, 99, 0, 0, Kind::Published}));
    assert(!bounds.append({99, 99, 0, 0, 0, Kind::Published}));
    assert(bounds.dropped == 1);
    size_t n = 0;
    bounds.visit([&](const Event& e) { assert(e.serial == ++n && e.producer_frame == 99); });
    assert(n == 4); // oldest evidence retained; no wrap/overwrite
    Buffer<4096> concurrent;
    std::thread writer([&] {
        for (uint64_t i = 1; i <= 4096; ++i)
            assert(concurrent.append({i, i, i * 7, i * 11, i * 13, Kind::Completed}));
    });
    // Visit during publication: acquire-ready is the sole permission to read.
    for (unsigned pass = 0; pass < 100; ++pass)
        concurrent.visit([](const Event& e) {
            assert(e.ns == e.serial && e.producer_frame == e.serial * 7);
            assert(e.a == e.serial * 11 && e.b == e.serial * 13);
        });
    writer.join();
    n = 0;
    concurrent.visit([&](const Event& e) { assert(e.serial == ++n); });
    assert(n == 4096 && concurrent.dropped == 0);
#ifdef _WIN32
    char folder[MAX_PATH], path[MAX_PATH];
    const DWORD length = GetTempPathA(MAX_PATH, folder);
    assert(length > 0 && length < MAX_PATH);
    assert(GetTempFileNameA(folder, "wwh", 0, path) != 0);
    assert(_putenv_s("WWHD_VK_HANDOFF_TRACE", path) == 0);
#else
    char path[] = "/tmp/wwhd-handoff-test-XXXXXX";
    const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    assert(setenv("WWHD_VK_HANDOFF_TRACE", path, 1) == 0);
#endif
    assert(enabled()); set_producer_frame(42);
    emit(Lane::Producer, Kind::Published, 9, producer_frame(), 1, 123);
    std::thread replay([] {
        set_worker_job(9, 42);
        emit_worker(Kind::QueueSubmitBegin);
        emit_worker(Kind::QueueSubmitEnd);
    });
    replay.join();
    set_worker_job(9, 42);
    const auto end = native_now_ns();
    slow_opcode(17, end-249999, end, 10, 50); // below threshold: no record
    slow_opcode(17, end-250000, end, 10, 50); // inclusive boundary
    slow_opcode(25, end-300000, end, 0, 0); // unavailable CPU stays zero
    assert(flush());
    emit(Lane::Producer, Kind::Published, 999); // closed endpoint cannot append
    assert(flush()); // repeated orderly shutdown preserves the same records
    std::ifstream input(path); std::string json((std::istreambuf_iterator<char>(input)), {});
    assert(json.find("\"unpublished\":0") != std::string::npos);
    assert(json.find("\"dropped\":0") != std::string::npos);
    assert(json.find(",9,42,1,123]") != std::string::npos);
    assert(json.find(",999,") == std::string::npos);
    assert(json.find("QueueSubmitBegin") != std::string::npos);
    assert(json.find(",17,250000]") != std::string::npos);
    assert(json.find(",17,40]") != std::string::npos);
    assert(json.find(",17,249999]") == std::string::npos);
    assert(json.find(",25,0]") != std::string::npos);
    assert(json.find("\"schema\":2") != std::string::npos);
    assert(json.find("vkCmdBlitImage") != std::string::npos);
    input.close(); // Windows does not allow removing this open stream.
    assert(std::remove(path) == 0);
}
