// Private setup-generated cache generations. The release template contains no
// code; these files are resolved from the player's game and stay on this device.
#pragma once
#include "shader_setup_hint.h"
#include "platform/host.h"
#include "platform/process.h"
#include "platform/shared_read.h"
#include "mods/mod_json.h"
#include "mods/mod_hash.h"
#include "gfx/vulkan/atomic_temporary.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <cstdio>
#include <chrono>
#include <sys/stat.h>
#include <memory>
#include <fcntl.h>
#if defined(__linux__) || defined(__ANDROID__)
#include <sys/syscall.h>
#endif
#ifdef _WIN32
#include <io.h>
#else
#include <sys/file.h>
#endif

namespace shaderprebuild {
namespace fs = std::filesystem;
inline std::string runtimeLibrary;
inline std::string resultPath;
inline mods::json::Value pendingIdentity;
inline bool pendingBound = false;
inline fs::path runtime_path() { return fs::u8path(runtimeLibrary.empty() ? host::executable_path() : runtimeLibrary); }
inline fs::path root() { return fs::u8path(host::config_dir()) / "shader-prebuild"; }
class PublicationLock {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    OVERLAPPED offset{};
#else
    int file = -1;
#endif
public:
    PublicationLock() {
        fs::create_directories(root());
#ifdef _WIN32
        auto path = (root() / "activation.lock").wstring();
        file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open shader activation lock");
#else
        file = open((root() / "activation.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (file < 0) throw std::runtime_error("cannot open shader activation lock");
#endif
        for (int attempt = 0; attempt < 500; ++attempt) {
#ifdef _WIN32
            if (LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset)) return;
#else
            if (!flock(file, LOCK_EX | LOCK_NB)) return;
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
#ifdef _WIN32
        CloseHandle(file); file = INVALID_HANDLE_VALUE;
#else
        close(file); file = -1;
#endif
        throw std::runtime_error("shader activation lock busy");
    }
    PublicationLock(const PublicationLock&) = delete;
    PublicationLock& operator=(const PublicationLock&) = delete;
    ~PublicationLock() {
#ifdef _WIN32
        if (file != INVALID_HANDLE_VALUE) { UnlockFileEx(file, 0, 1, 0, &offset); CloseHandle(file); }
#else
        if (file >= 0) { flock(file, LOCK_UN); close(file); }
#endif
    }
};
// setup's shader_prebuild.py replaces these manifests while the game may read them (issue #282)
inline mods::json::Value read(const fs::path& path) {
    std::string text;
    if (!host::read_shared(path, text, 1024 * 1024))
        throw std::runtime_error("shader preparation manifest unavailable or too large");
    return mods::json::parse(text);
}
inline bool requested() { return fs::exists(root() / "rebuild.request"); }
inline bool request_rebuild() {
    try {
        fs::create_directories(root());
        std::ofstream file(root() / "rebuild.request", std::ios::trunc);
        file << "Prepare shaders optionally using setup repair.\n";
        return bool(file);
    } catch (...) { return false; }
}
inline void set_default(const char* name, const fs::path& value) {
    if (std::getenv(name)) return;
#ifdef _WIN32
    auto native = value;
    native.make_preferred();
    _wputenv_s(host::process_wide(name).c_str(), native.c_str());
#else
    setenv(name, value.string().c_str(), 0);
#endif
}
inline fs::path generation(const mods::json::Value& active) {
    const auto name = active.get("generation").string();
    if (active.get("schema").number != 1 || name.empty() ||
        name.rfind("generation-", 0) || fs::path(name).filename() != fs::path(name) ||
        name.find('\\') != std::string::npos || name.find('\0') != std::string::npos)
        throw std::runtime_error("invalid shader cache generation");
    auto path = root() / name;
    if (fs::is_symlink(path)) throw std::runtime_error("shader cache generation is a symlink");
    return path;
}
// Metadata is a launch freshness check, not a content authenticator. Full hashes
// are produced only by explicit setup/rebuild. Missing legacy metadata fails closed.
inline mods::json::Value metadata(const fs::path& path) {
    mods::json::Value value;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
        throw std::runtime_error("cache input unavailable");
    const uint64_t ticks = (uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    const uint64_t unixTicks = ticks - 116444736000000000ull;
    value["size"] = double((uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
    value["mtime"] = double(unixTicks / 10000000);
    value["mtime_ns"] = double((unixTicks % 10000000) * 100);
#else
    struct stat info{};
    if (stat(path.c_str(), &info)) throw std::runtime_error("cache input unavailable");
    value["size"] = double(info.st_size); value["mtime"] = double(info.st_mtime);
#ifdef __APPLE__
    value["mtime_ns"] = double(info.st_mtimespec.tv_nsec);
#else
    value["mtime_ns"] = double(info.st_mtim.tv_nsec);
#endif
#endif
    return value;
}
// Which renderers a generation was warmed for. macOS setup warms Metal and Vulkan (MoltenVK) for
// one shared head start and records each result in "renderers" ({"metal": "ready", "vulkan":
// "unavailable"}); older manifests (and Windows/Linux/Android) name the one renderer in the identity.
inline bool renderer_prepared(const mods::json::Value& active, const std::string& renderer) {
    const auto& renderers = active.get("renderers");
    if (renderers.type == mods::json::Value::Object) return renderers.get(renderer).string() == "ready";
    return active.get("identity").get("renderer").string() == renderer;
}
inline bool identity_matches(const mods::json::Value& active, const fs::path& game,
                             const std::string& renderer, bool optOut = false) {
    const auto& id = active.get("identity");
    const auto& launch = active.get("launch");
    if (id.get("schema").number != 1 || (!optOut && !renderer_prepared(active, renderer))) return false;
    const auto launcher = read(root() / "launcher.json");
    const fs::path package = fs::u8path(launcher.get("package").string());
    if (!(launch.get("template") == metadata(fs::u8path(launcher.get("template").string())))) return false;
    if (!optOut && (!(launch.get("runtime") == metadata(runtime_path())) ||
        !(launch.get("translator") == metadata(package / "tools/shaderprep.py")) ||
        !(launch.get("format") == metadata(package / "tools/shaderprep_format.py")))) return false;
    const auto& inventory = launch.get("game");
    if (inventory.type != mods::json::Value::Object || inventory.object.empty()) return false;
    size_t count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(game)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext != ".rpx" && ext != ".sharcfb" && ext != ".szs" && ext != ".pack" && ext != ".sarc") continue;
        ++count;
        const auto bytes = entry.path().lexically_relative(game).generic_u8string();
        const std::string key(bytes.begin(), bytes.end());
        if (!(inventory.get(key) == metadata(entry.path()))) return false;
    }
    return count == inventory.object.size();
}
// Opt-out survives runtime/translator updates; game/template changes or an
// explicit rebuild request permit preparation in setup again.
inline bool skipped(const fs::path& game, const std::string& renderer) {
    if (requested()) return false;
    try {
        const auto marker = read(root() / "skip.json");
        return marker.get("schema").number == 1 && identity_matches(marker, game, renderer, true);
    } catch (...) { return false; }
}
// Keep selected headstart files alive while another setup switches generations.
class GenerationLease {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    OVERLAPPED offset{};
#else
    int file = -1;
#endif
    bool locked = false;
public:
    GenerationLease() = default;
    GenerationLease(const GenerationLease&) = delete;
    GenerationLease& operator=(const GenerationLease&) = delete;
    bool acquire(const fs::path& dir) {
        release();
#ifdef _WIN32
        file = CreateFileW((dir / "in-use.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        locked = file != INVALID_HANDLE_VALUE && LockFileEx(file, LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset);
#else
        file = open((dir / "in-use.lock").c_str(), O_CREAT | O_RDWR, 0600);
        locked = file >= 0 && flock(file, LOCK_SH | LOCK_NB) == 0;
#endif
        if (!locked) release();
        return locked;
    }
    void release() noexcept {
#ifdef _WIN32
        if (file != INVALID_HANDLE_VALUE) {
            if (locked) UnlockFileEx(file, 0, 1, 0, &offset);
            CloseHandle(file); file = INVALID_HANDLE_VALUE;
        }
#else
        if (file >= 0) {
            if (locked) flock(file, LOCK_UN);
            close(file); file = -1;
        }
#endif
        locked = false;
    }
    ~GenerationLease() { release(); }
};
inline std::unique_ptr<GenerationLease> generationLease;
// Normal uses atomic no-replace operations first. The mode is a deterministic
// unit-test seam; runtime callers always use Normal.
enum class LegacyPublishMode { Normal, ExclusiveCopy, Fail };
inline bool publish_legacy_file(const fs::path& temporary, const fs::path& target,
                                LegacyPublishMode mode = LegacyPublishMode::Normal,
                                void (*beforeFailureCleanup)(const fs::path&) = nullptr) {
    if (mode == LegacyPublishMode::Fail) return false;
    if (mode == LegacyPublishMode::Normal) {
#ifdef _WIN32
        if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
#else
        std::error_code error;
        fs::create_hard_link(temporary, target, error);
        if (!error) return true;
#ifdef __APPLE__
        if (renamex_np(temporary.c_str(), target.c_str(), RENAME_EXCL) == 0) return true;
#elif defined(SYS_renameat2)
        if (syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD,
                    target.c_str(), 1 /* RENAME_NOREPLACE */) == 0) return true;
#endif
#endif
    }
    // Last resort for filesystems lacking links and no-replace rename. Own the
    // exclusive destination before copying, and remove only our failed output.
    const auto targetBytes = target.u8string();
    const std::string targetPath(targetBytes.begin(), targetBytes.end());
    FILE* output = gfxvk::open_utf8_file(targetPath, "wbx");
    if (!output) return false;
    std::unique_ptr<FILE, decltype(&std::fclose)> outputOwner(output, &std::fclose);
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION owned{};
    const bool identified=GetFileInformationByHandle(
        reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(output))),&owned)!=0;
#else
    struct stat owned{};
    const bool identified=fstat(fileno(output),&owned)==0;
#endif
    const auto sourceBytes = temporary.u8string();
    FILE* input = gfxvk::open_utf8_file(std::string(sourceBytes.begin(),sourceBytes.end()), "rb");
    std::unique_ptr<FILE, decltype(&std::fclose)> inputOwner(input, &std::fclose);
    bool ok = input != nullptr;
    if (input) {
        char block[65536]; size_t size;
        while ((size = std::fread(block,1,sizeof block,input)))
            if (std::fwrite(block,1,size,output) != size) { ok=false; break; }
        if (std::ferror(input)) ok=false;
        if (std::fclose(inputOwner.release())) ok=false;
    }
    if (std::fclose(outputOwner.release())) ok=false;
    if (!ok) {
        if (beforeFailureCleanup) beforeFailureCleanup(target);
        // Another renderer can atomically replace this pathname while copying.
        // Never remove a replacement which is not the file we created.
        bool same=false;
#ifdef _WIN32
        HANDLE current=CreateFileW(target.c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(current!=INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};
            same=identified && GetFileInformationByHandle(current,&info) &&
                info.dwVolumeSerialNumber==owned.dwVolumeSerialNumber &&
                info.nFileIndexHigh==owned.nFileIndexHigh && info.nFileIndexLow==owned.nFileIndexLow;
            CloseHandle(current);
        }
#else
        struct stat current{};
        same=identified && lstat(target.c_str(),&current)==0 &&
            current.st_dev==owned.st_dev && current.st_ino==owned.st_ino;
#endif
        if (same) { std::error_code error; fs::remove(target,error); }
    }
    return ok;
}
inline bool copy_legacy_file(const fs::path& source, const fs::path& target,
                             LegacyPublishMode mode = LegacyPublishMode::Normal) {
    std::error_code error;
    const auto targetStatus=fs::symlink_status(target,error);
    if (error && error != std::errc::no_such_file_or_directory) return false;
    if (fs::exists(targetStatus)) return !fs::is_directory(targetStatus);
    error.clear();
    const auto sourceStatus=fs::symlink_status(source,error);
    if (error) return error == std::errc::no_such_file_or_directory;
    if (!fs::is_regular_file(sourceStatus)) return true;
    fs::create_directories(target.parent_path());
    std::string temporary;
    const auto targetBytes=target.u8string();
    FILE* staged = gfxvk::open_atomic_temporary(std::string(targetBytes.begin(),targetBytes.end()), temporary);
    if (std::fclose(staged)) { fs::remove(fs::u8path(temporary),error); return false; }
    bool copied=false;
    try {
        fs::copy_file(source, fs::u8path(temporary), fs::copy_options::overwrite_existing);
        copied=publish_legacy_file(fs::u8path(temporary), target, mode);
        if (!copied) {
            error.clear();
            const auto status=fs::symlink_status(target,error);
            copied=!error && fs::exists(status) && !fs::is_directory(status);
        }
    } catch (...) {
        fs::remove(fs::u8path(temporary), error);
        throw;
    }
    fs::remove(fs::u8path(temporary), error);
    return copied;
}
inline bool legacy_pipeline_filename(const std::string& name) {
    // Only our device-specific caches and their adjacent recipe files: vk-<12 hex>.bin
    // (gfxvk::device_pipeline_cache_filename) and the earlier vulkan-<vendor>-<device>-<uuid>.bin.
    if (name.starts_with("vk-")) {
        if (name.size() != 19 && name.size() != 27) return false;
        if (name.substr(15, 4) != ".bin" || (name.size() == 27 && name.substr(19) != ".recipes")) return false;
        for (size_t i = 3; i < 15; ++i)
            if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
        return true;
    }
    constexpr size_t baseSize = 7 + 8 + 1 + 8 + 1 + 32 + 4;
    if (name.size() != baseSize && name.size() != baseSize + 8) return false;
    if (!name.starts_with("vulkan-") || name[15] != '-' || name[24] != '-' ||
        name.substr(57, 4) != ".bin" || (name.size() > baseSize && name.substr(baseSize) != ".recipes")) return false;
    for (size_t i = 7; i < 57; ++i) {
        if (i == 15 || i == 24) continue;
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
    }
    return true;
}
inline void migrate_default_caches(const fs::path& configDirectory, const fs::path& metalCacheFile,
                                    LegacyPublishMode mode = LegacyPublishMode::Normal) noexcept {
    try {
        const auto live = root() / "live", marker = live / ".legacy-defaults-v1";
        if (fs::exists(marker)) return;
        PublicationLock migrationLock;
        if (fs::exists(marker)) return; // Another process completed while waiting.
        bool complete=true;
        auto copy = [&](const fs::path& source, const fs::path& target) {
            try {
                if (!copy_legacy_file(source, target, mode)) {
                    complete=false;
                    std::fprintf(stderr,"[shader prebuild] Cannot migrate [%s] to [%s]\n", mods::json::quote(source.string()).c_str(), mods::json::quote(target.string()).c_str());
                }
            } catch (const std::exception& e) {
                complete=false;
                std::fprintf(stderr,"[shader prebuild] Cannot migrate [%s] to [%s]: %s\n", mods::json::quote(source.string()).c_str(), mods::json::quote(target.string()).c_str(), e.what());
            }
        };
        const auto old = configDirectory / "shadercache";
        std::error_code error;
        fs::directory_iterator entries(old, error), end;
        for (; !error && entries != end; entries.increment(error)) {
            const auto name = entries->path().filename().string();
            if (legacy_pipeline_filename(name)) copy(entries->path(), live / name);
        }
        if (error && error != std::errc::no_such_file_or_directory) complete=false;
        // macOS portable builds previously selected shaders.bin automatically,
        // which placed Vulkan device caches beside it instead of in shadercache.
        error.clear();
        fs::directory_iterator portable(configDirectory, error);
        for (; !error && portable != end; portable.increment(error)) {
            const auto name = portable->path().filename().string();
            constexpr std::string_view prefix = "shaders.bin.";
            if (name.starts_with(prefix) && legacy_pipeline_filename(name.substr(prefix.size())))
                copy(portable->path(), live / name.substr(prefix.size()));
        }
        if (error && error != std::errc::no_such_file_or_directory) complete=false;
        copy(configDirectory / "shaders.bin", live / "shaders.bin");
        copy(old / "vulkan-shaders/spirv.bin", live / "vulkan-shaders/spirv.bin");
        if (!metalCacheFile.empty()) copy(metalCacheFile, live / "shaders.bin");
        if (!complete) {
            std::fprintf(stderr,"[shader prebuild] Legacy cache migration incomplete; will retry\n");
            return;
        }
        fs::create_directories(live);
        std::string temporary;
        FILE* staged = gfxvk::open_atomic_temporary(marker.string(), temporary);
        const bool written = std::fputs("legacy-defaults-v1\n", staged) >= 0;
        const bool closed = std::fclose(staged) == 0;
        if (written && closed) publish_legacy_file(fs::u8path(temporary), marker, mode);
        fs::remove(fs::u8path(temporary), error);
    } catch (...) {
        std::fprintf(stderr, "[shader prebuild] Legacy cache migration unavailable; continuing\n");
    }
}
inline void bind_live_caches() {
    fs::path metal;
#ifdef __APPLE__
    const char* home = std::getenv("HOME");
    metal = fs::u8path(home ? home : ".") / "Library/Caches/wwhd/shaders.bin";
#endif
    migrate_default_caches(fs::u8path(host::config_dir()), metal);

    set_default("WWHD_SHADER_CACHE", root() / "live/shaders.bin");
    set_default("WWHD_VK_SHADER_CACHE", root() / "live/vulkan-shaders");
    set_default("WWHD_VK_PIPELINE_CACHE_DIR", root() / "live");
}
// Setup supplies explicit paths, so it bypasses normal generation activation.
// Import before warming can create files that would hide legacy player entries.
// Custom test directories must not touch the normal user's cache namespace.
inline void bind_setup_live_caches() {
    const auto directory = gfxvk::path_environment_utf8("WWHD_VK_PIPELINE_CACHE_DIR");
    if (directory && fs::u8path(*directory).lexically_normal() == (root() / "live").lexically_normal())
        bind_live_caches();
}
inline void bind_generation(const fs::path& dir) {
    set_default("WWHD_HEADSTART", dir / "headstart.bin");
    bind_live_caches();
}
inline bool activate_pending(const fs::path& game, const std::string& renderer) {
    bind_live_caches();
    if (std::getenv("WWHD_HEADSTART") || skipped(game, renderer)) return false;
    try {
        const auto active = read(root() / "active.json");
        if (active.get("status").string() != "warm_pending" || !identity_matches(active, game, renderer)) return false;
        const auto dir = generation(active);
        if (!fs::is_regular_file(dir / "headstart.bin")) return false;
        auto lease = std::make_unique<GenerationLease>();
        if (!lease->acquire(dir)) return false;
        generationLease = std::move(lease);
        bind_generation(dir); pendingIdentity = active; pendingBound = true; return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shaders] Pending preparation unavailable: %s\n", e.what()); return false;
    }
}
inline bool atomic_json(const fs::path& path, const mods::json::Value& value) {
    fs::create_directories(path.parent_path());
    const auto bytes = path.u8string();
    const std::string destination(bytes.begin(), bytes.end());
    std::string temporary;
    const auto text = mods::json::dump(value) + '\n';
    std::unique_ptr<FILE, decltype(&std::fclose)> file(
        gfxvk::open_atomic_temporary(destination, temporary), &std::fclose);
    if (!file) return false;
    const bool written = std::fwrite(text.data(), 1, text.size(), file.get()) == text.size();
    const bool closed = std::fclose(file.release()) == 0;
    const bool success = written && closed && host::replace_file(temporary, destination);
    if (!success) { std::error_code ec; fs::remove(fs::u8path(temporary), ec); }
    return success;
}
inline void cleanup_generations(const fs::path& selected) {
    try {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root(), ec)) {
        const auto dir = entry.path();
        if (dir == selected || entry.is_symlink() || !entry.is_directory() ||
            dir.filename().string().rfind("generation-", 0) != 0 ||
            !fs::is_regular_file(dir / "manifest.json")) continue;
#ifdef _WIN32
        HANDLE file = CreateFileW((dir / "in-use.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        OVERLAPPED offset{};
        const bool available = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset);
        if (available) UnlockFileEx(file, 0, 1, 0, &offset);
        // FILE_SHARE_DELETE allows marking a file for deletion, but Windows
        // cannot remove its parent directory until the last handle closes.
        CloseHandle(file);
        if (available) {
            fs::remove_all(dir, ec);
            if (ec) std::fprintf(stderr, "[shader prebuild] Cannot remove generation [%s]: %s\n", dir.string().c_str(), ec.message().c_str());
        }
#else
        const int file = open((dir / "in-use.lock").c_str(), O_CREAT | O_RDWR, 0600);
        if (file < 0) continue;
        if (flock(file, LOCK_EX | LOCK_NB) == 0) fs::remove_all(dir, ec);
        close(file);
#endif
    }
    } catch (const fs::filesystem_error&) { /* Cleanup never invalidates a successful switch. */ }
}
inline bool finish_pending(bool success) {
    try {
        if (success) {
            PublicationLock lock;
            auto active = read(root() / "active.json");
            const auto dir = generation(active);
            // Commit the generation before publishing ready to the setup UI.
            if (!pendingBound || !(active == pendingIdentity) || active.get("status").string() != "warm_pending") success = false;
            else {
                active["status"] = "ready";
                success = atomic_json(dir / "manifest.json", active) && atomic_json(root() / "active.json", active);
                if (success) { std::error_code ec; fs::remove(root() / "rebuild.request", ec); fs::remove(root() / "failed.json", ec); fs::remove(root() / "skip.json", ec); cleanup_generations(dir); }
            }
        }
        if (!success && pendingBound) {
            auto failed = pendingIdentity;
            failed["status"] = "failed";
            atomic_json(root() / "failed.json", failed);
        }
        if (!resultPath.empty()) {
            mods::json::Value result; result["schema"] = 1;
            result["status"] = success ? "ready" : "failed";
            success = atomic_json(fs::u8path(resultPath), result) && success;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shaders] Cannot publish preparation result: %s\n", e.what()); return false;
    }
    return success;
}
// Call before renderer initialization. Explicit test/user cache paths win. A
// rebuild publishes a fresh generation; live caches are never deleted by UI.
inline bool activate(const fs::path& game, const std::string& renderer) {
    // Live caches survive invalidation/rebuild and remain useful without a
    // prepared generation. Explicit user/test overrides still win.
    bind_live_caches();
    if (std::getenv("WWHD_HEADSTART") || skipped(game, renderer)) return false;
    try {
        const auto active = read(root() / "active.json");
        // Launch never assembles or warms shaders. Updates/rebuilds belong to
        // setup's visible, cancellable preparation step.
        if (requested() || active.get("status").string() != "ready" ||
            !identity_matches(active, game, renderer)) return false;
        const auto dir = generation(active);
        if (!fs::is_regular_file(dir / "headstart.bin")) return false;
        auto lease = std::make_unique<GenerationLease>();
        if (!lease->acquire(dir)) return false;
        generationLease = std::move(lease);
        bind_generation(dir);
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shaders] %s\n", e.what()); return false;
    }
}
} // namespace shaderprebuild
