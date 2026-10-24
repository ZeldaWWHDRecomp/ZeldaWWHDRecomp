// Reads a small file that another process replaces atomically while we read it (issue #282).
// Setup publishes code-mods-status.json, guest-sdk.json and the shader-preparation manifests with a
// write-to-temporary-then-rename; the game polls them. On Windows the handle allows FILE_SHARE_DELETE:
// setup's POSIX-semantics rename (FileRenameInfoEx, tools/installer/code_mods.py atomic_replace) can then
// replace the file while it is open here, and this read keeps the old contents. The handle is held only
// for the read itself.
#pragma once
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace host {
// false: missing, unreadable, or larger than `limit` bytes (also a file that grows past it while read)
inline bool read_shared(const std::filesystem::path& path, std::string& data, size_t limit) {
    data.clear();
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    char block[16384];
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(file, block, DWORD(sizeof block), &count, nullptr)) { ok = false; break; }
        if (count == 0) break;
        if (data.size() + count > limit) { ok = false; break; }
        data.append(block, count);
    }
    CloseHandle(file);
    if (!ok) data.clear();
    return ok;
#else
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    data.resize(limit + 1);
    file.read(data.data(), std::streamsize(data.size()));
    data.resize(size_t(file.gcount()));
    if (file.bad() || data.size() > limit) { data.clear(); return false; }
    return true;
#endif
}
}
