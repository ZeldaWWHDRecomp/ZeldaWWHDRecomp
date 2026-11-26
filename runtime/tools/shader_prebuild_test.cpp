// Synthetic files only: activation/invalidation must never touch real caches.
#include "gfx/shader_prebuild.h"
#include <cassert>
#include <chrono>
#include <iostream>
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--measure-launch") {
        const auto active = shaderprebuild::read(shaderprebuild::root() / "active.json");
        for (int i = 0; i < 3; ++i) {
            const auto start = std::chrono::steady_clock::now();
            const bool valid = shaderprebuild::identity_matches(active, std::filesystem::u8path(argv[2]), "vulkan");
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::printf("metadata_check_ms=%.3f valid=%d\n", elapsed, valid);
            if (!valid) return 2;
        }
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--json-roundtrip") {
        std::string input(std::istreambuf_iterator<char>(std::cin), {});
        try {
            const auto output = mods::json::dump(mods::json::parse(input));
            return std::fwrite(output.data(), 1, output.size(), stdout) == output.size() ? 0 : 1;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "JSON round-trip failed: %s\n", error.what()); return 1;
        }
    }
    // If startup unexpectedly spawns this test as its helper, leave evidence.
    if (argc > 1) {
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--cache-root") {
                std::ofstream(std::filesystem::path(argv[i + 1]) / "unexpected-spawn") << "spawned";
                return 3;
            }
        return 3;
    }
    namespace fs = std::filesystem;
    using mods::json::Value;
    const auto testRoot = fs::temp_directory_path() / ("wwhd-prebuild-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto temporary = testRoot;
#ifndef _WIN32
    // Literal backslashes on POSIX exercise Windows-style JSON paths in the
    // entire activation/migration flow, rather than only a serializer fixture.
    temporary /= R"(a\tmp\retry\vulkan\failed\100%shader)";
#endif
#ifdef _WIN32
    _putenv_s("WWHD_USER_DIR", temporary.string().c_str());
    _putenv_s("WWHD_HEADSTART", "");
#else
    setenv("WWHD_USER_DIR", temporary.string().c_str(), 1);
    unsetenv("WWHD_HEADSTART");
#endif
    const auto package = temporary / "package", game = temporary / "game";
    auto env = [](const char* name, const char* value = nullptr) {
#ifdef _WIN32
        _putenv_s(name, value ? value : "");
#else
        if(value) setenv(name,value,1); else unsetenv(name);
#endif
    };
    auto clear_live = [&] {
        for(auto name : {"WWHD_SHADER_CACHE","WWHD_VK_SHADER_CACHE","WWHD_VK_PIPELINE_CACHE","WWHD_VK_PIPELINE_CACHE_DIR"}) env(name);
    };
    auto assert_path_env = [](const char* name, const fs::path& expected) {
        const auto value = gfxvk::path_environment_utf8(name);
        const auto actual = value ? fs::u8path(*value).lexically_normal() : fs::path();
        const auto normalized = expected.lexically_normal();
        if (!value || actual != normalized)
            std::fprintf(stderr, "%s path mismatch: actual=[%s] expected=[%s]\n",
                name, value ? value->c_str() : "<unset>", expected.string().c_str());
        assert(value && actual == normalized);
    };
    auto assert_live = [&] {
        assert_path_env("WWHD_SHADER_CACHE", shaderprebuild::root()/"live/shaders.bin");
        assert_path_env("WWHD_VK_SHADER_CACHE", shaderprebuild::root()/"live/vulkan-shaders");
        assert_path_env("WWHD_VK_PIPELINE_CACHE_DIR", shaderprebuild::root()/"live");
        assert(!getenv("WWHD_VK_PIPELINE_CACHE"));
        assert(!getenv("WWHD_HEADSTART"));
    };
    // Isolate the Metal default before any activation can inspect HOME.
    const auto home = temporary / "synthetic-home";
    env("HOME",home.string().c_str());
    auto write = [](const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path()); std::ofstream(path) << text;
    };
    auto bytes = [](const fs::path& path) {
        std::ifstream file(path,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file),{});
    };
    const auto config = fs::u8path(host::config_dir());
    const auto legacy = config / "shadercache";
    const std::string pipeline = "vulkan-12345678-abcdef01-0123456789abcdef0123456789abcdef.bin";
    const std::string preserved = "vulkan-87654321-abcdef01-0123456789abcdef0123456789abcdef.bin";
    const std::string later = "vulkan-99999999-abcdef01-0123456789abcdef0123456789abcdef.bin";
    write(legacy/pipeline,"legacy pipeline bytes");
    write(legacy/(pipeline+".recipes"),"legacy recipe bytes");
    write(legacy/preserved,"older source bytes");
    write(shaderprebuild::root()/"live"/preserved,"existing live bytes");
    write(legacy/"vulkan-shaders/spirv.bin","legacy SPIR-V bytes");
    write(legacy/"vulkan-invalid.bin","unrelated bytes");
    write(legacy/(pipeline+".tmp"),"unfinished bytes");
    const auto metal = home/"Library/Caches/wwhd/shaders.bin";
    write(metal,"legacy Metal bytes");
    clear_live();
    // A custom setup/test cache directory must not import normal user caches.
    env("WWHD_VK_PIPELINE_CACHE_DIR", (temporary/"custom-warm").string().c_str());
    shaderprebuild::bind_setup_live_caches();
    assert(!fs::exists(shaderprebuild::root()/"live"/pipeline));
    // Standard setup warming imports before its renderer creates live files.
    env("WWHD_VK_PIPELINE_CACHE_DIR", (shaderprebuild::root()/"live").string().c_str());
    shaderprebuild::bind_setup_live_caches();
    assert(bytes(shaderprebuild::root()/"live"/pipeline)=="legacy pipeline bytes");
    assert(bytes(shaderprebuild::root()/"live/vulkan-shaders/spirv.bin")=="legacy SPIR-V bytes");
    clear_live();
    // No manifest/generation must still reuse the stable live caches.
    assert(!shaderprebuild::activate(game,"vulkan")); assert_live();
    const auto live=shaderprebuild::root()/"live";
    assert(bytes(fs::u8path(getenv("WWHD_VK_PIPELINE_CACHE_DIR"))/pipeline)=="legacy pipeline bytes");
    assert(bytes(live/(pipeline+".recipes"))=="legacy recipe bytes");
    assert(bytes(fs::u8path(getenv("WWHD_VK_SHADER_CACHE"))/"spirv.bin")=="legacy SPIR-V bytes");
    assert(bytes(live/preserved)=="existing live bytes");
    assert(bytes(legacy/preserved)=="older source bytes");
    assert(bytes(legacy/pipeline)=="legacy pipeline bytes");
    assert(bytes(legacy/(pipeline+".recipes"))=="legacy recipe bytes");
    assert(bytes(legacy/"vulkan-shaders/spirv.bin")=="legacy SPIR-V bytes");
    assert(bytes(metal)=="legacy Metal bytes");
#ifdef __APPLE__
    assert(bytes(fs::u8path(getenv("WWHD_SHADER_CACHE")))=="legacy Metal bytes");
#endif
    assert(!fs::exists(live/"vulkan-invalid.bin"));
    // device caches under the current short name (issue #144) are recognised too
    assert(shaderprebuild::legacy_pipeline_filename("vk-0123456789ab.bin"));
    assert(shaderprebuild::legacy_pipeline_filename("vk-0123456789ab.bin.recipes"));
    assert(!shaderprebuild::legacy_pipeline_filename("vk-0123456789aB.bin"));
    assert(!shaderprebuild::legacy_pipeline_filename("vk-0123456789ab.bin.t0123abcd"));
    assert(shaderprebuild::legacy_pipeline_filename(pipeline));
    assert(!fs::exists(live/(pipeline+".tmp")));
    assert(fs::is_regular_file(live/".legacy-defaults-v1"));
    write(legacy/later,"new legacy cache after migration");
    clear_live();
    assert(!shaderprebuild::activate_pending(game,"vulkan")); assert_live();
    assert(!fs::exists(live/later)); // The one-time marker prevents re-import.
    assert(bytes(legacy/later)=="new legacy cache after migration");
    // Explicit cache and headstart overrides remain authoritative.
    env("WWHD_SHADER_CACHE","explicit-shaders");
    env("WWHD_VK_SHADER_CACHE","explicit-spirv");
    env("WWHD_VK_PIPELINE_CACHE","0");
    env("WWHD_HEADSTART","explicit-headstart");
    assert(!shaderprebuild::activate(game,"vulkan"));
    assert(std::string(getenv("WWHD_SHADER_CACHE"))=="explicit-shaders");
    assert(std::string(getenv("WWHD_VK_SHADER_CACHE"))=="explicit-spirv");
    assert(std::string(getenv("WWHD_VK_PIPELINE_CACHE"))=="0");
    assert(std::string(getenv("WWHD_HEADSTART"))=="explicit-headstart");
    env("WWHD_HEADSTART"); clear_live();
    auto json = [&](const fs::path& path, const Value& v) { write(path, mods::json::dump(v)); };
    write(package / "tools/shadercache/template.bin", "synthetic state fixture");
    write(package / "tools/shaderprep.py", "synthetic translator fixture");
    write(package / "tools/shaderprep_format.py", "synthetic schema fixture");
    write(game / "code/test.rpx", "synthetic executable fixture");
    Value launcher; launcher["schema"] = 1; launcher["package"] = package.string();
    launcher["helper"] = (package / "tools/installer/shader_prebuild.py").string();
    launcher["template"] = (package / "tools/shadercache/template.bin").string();
    json(shaderprebuild::root() / "launcher.json", launcher);
    Value active; active["schema"] = 1; active["status"] = "ready";
    active["generation"] = "generation-test";
    Value id; id["schema"] = 1; id["renderer"] = "vulkan";
    id["runtime"] = mods::hash::sha256_file(host::executable_path());
    id["template"] = mods::hash::sha256_file(package / "tools/shadercache/template.bin");
    id["translator"] = mods::hash::sha256_file(package / "tools/shaderprep.py");
    id["format"] = mods::hash::sha256_file(package / "tools/shaderprep_format.py");
    id["game"]["code/test.rpx"] = mods::hash::sha256_file(game / "code/test.rpx");
    active["identity"] = id;
    Value launch;
    launch["runtime"] = shaderprebuild::metadata(shaderprebuild::runtime_path());
    launch["template"] = shaderprebuild::metadata(package / "tools/shadercache/template.bin");
    launch["translator"] = shaderprebuild::metadata(package / "tools/shaderprep.py");
    launch["format"] = shaderprebuild::metadata(package / "tools/shaderprep_format.py");
    launch["game"]["code/test.rpx"] = shaderprebuild::metadata(game / "code/test.rpx");
    active["launch"] = launch;
    write(shaderprebuild::root() / "generation-test/headstart.bin", "synthetic local cache");
    json(shaderprebuild::root() / "active.json", active);
    assert(shaderprebuild::identity_matches(active, game, "vulkan"));
    assert(!shaderprebuild::identity_matches(active, game, "metal"));
    assert(shaderprebuild::activate(game, "vulkan"));
    assert_path_env("WWHD_HEADSTART", shaderprebuild::root() / "generation-test/headstart.bin");
    {
        // macOS: one generation warmed per renderer; each start uses it only if its renderer is ready
        auto both = active;
        both["identity"]["renderer"] = "metal";
        both["renderers"]["metal"] = "ready"; both["renderers"]["vulkan"] = "ready";
        assert(shaderprebuild::identity_matches(both, game, "metal"));
        assert(shaderprebuild::identity_matches(both, game, "vulkan"));
        both["renderers"]["vulkan"] = "unavailable";
        assert(shaderprebuild::identity_matches(both, game, "metal"));
        assert(!shaderprebuild::identity_matches(both, game, "vulkan"));
        json(shaderprebuild::root() / "active.json", both);
        env("WWHD_HEADSTART");
        assert(!shaderprebuild::activate(game, "vulkan"));
        assert(shaderprebuild::activate(game, "metal"));
        assert_path_env("WWHD_HEADSTART", shaderprebuild::root() / "generation-test/headstart.bin");
        both["renderers"]["metal"] = "failed"; both["renderers"]["vulkan"] = "ready";
        assert(!shaderprebuild::identity_matches(both, game, "metal"));
        assert(shaderprebuild::identity_matches(both, game, "vulkan"));
        json(shaderprebuild::root() / "active.json", active);
    }
    {
        // A generation keyed by the pipeline identity (setup since incremental updates) survives another binary of
        // the same renderer, and is refused by a renderer whose identity differs
        auto keyed = active;
        keyed["launch"].object.erase("runtime");
        keyed["launch"]["pipeline"] = std::string(wwhd::pipeline_runtime_identity());
        assert(shaderprebuild::identity_matches(keyed, game, "vulkan"));
        keyed["launch"]["pipeline"] = std::string(64, '0');
        assert(!shaderprebuild::identity_matches(keyed, game, "vulkan"));
        auto stale = active;
        stale["launch"]["runtime"]["size"] = 1.0;  // an older generation: still checked by the binary's file
        assert(!shaderprebuild::identity_matches(stale, game, "vulkan"));
    }
    // Selected external templates participate in identity; bundled changes do not.
    const auto externalTemplate = temporary / "external-template.bin";
    write(externalTemplate, "synthetic state fixture");
    active["launch"]["template"] = shaderprebuild::metadata(externalTemplate);
    launcher["template"] = externalTemplate.string();
    launcher["python"] = host::executable_path();
    json(shaderprebuild::root() / "launcher.json", launcher);
    write(package / "tools/shadercache/template.bin", "different bundled fixture");
    assert(shaderprebuild::identity_matches(active, game, "vulkan"));
    write(externalTemplate, "changed external fixture");
    assert(!shaderprebuild::identity_matches(active, game, "vulkan"));
    write(package / "tools/installer/shader_prebuild.py", "synthetic helper fixture");
    fs::remove(externalTemplate);
#ifdef _WIN32
    _putenv_s("WWHD_HEADSTART", "");
#else
    unsetenv("WWHD_HEADSTART");
#endif
    clear_live();
    assert(!shaderprebuild::activate(game, "vulkan")); assert_live();
    assert(!fs::exists(shaderprebuild::root() / "unexpected-spawn"));
    launcher["template"] = (package / "tools/shadercache/template.bin").string();
    json(shaderprebuild::root() / "launcher.json", launcher);
    write(package / "tools/shadercache/template.bin", "synthetic state fixture");
    active["launch"]["template"] = shaderprebuild::metadata(package / "tools/shadercache/template.bin");
    // A new archive and changed code both invalidate; no old generation removed.
    write(game / "content/new.pack", "synthetic archive fixture");
    assert(!shaderprebuild::identity_matches(active, game, "vulkan"));
    fs::remove(game / "content/new.pack");
    write(game / "code/test.rpx", "changed synthetic executable fixture");
    assert(!shaderprebuild::identity_matches(active, game, "vulkan"));
    active["generation"] = "../outside";
    bool rejected = false;
    try { shaderprebuild::generation(active); } catch (...) { rejected = true; }
    assert(rejected);
    assert(shaderprebuild::request_rebuild() && shaderprebuild::requested());
    clear_live();
    assert(!shaderprebuild::activate(game,"vulkan")); assert_live();
    assert(fs::is_regular_file(shaderprebuild::root() / "generation-test/headstart.bin"));
    write(game / "code/test.rpx", "synthetic executable fixture");
    active["launch"]["game"]["code/test.rpx"] = shaderprebuild::metadata(game / "code/test.rpx");
    active["generation"] = "generation-test"; active["status"] = "warm_pending";
    json(shaderprebuild::root() / "active.json", active);
#ifdef _WIN32
    _putenv_s("WWHD_HEADSTART", "");
#else
    unsetenv("WWHD_HEADSTART");
#endif
    shaderprebuild::resultPath = (temporary / "job/result.json").string();
    // Deliberate cancellation survives a pending generation, but rebuild wins.
    fs::remove(shaderprebuild::root() / "rebuild.request");
    Value skip; skip["schema"] = 1; skip["identity"] = id; skip["launch"] = active.get("launch");
    json(shaderprebuild::root() / "skip.json", skip);
    assert(shaderprebuild::skipped(game, "vulkan"));
    assert(!shaderprebuild::activate_pending(game, "vulkan"));
    assert(!shaderprebuild::activate(game, "vulkan"));
    assert(shaderprebuild::skipped(game, "metal"));
    auto runtimeChangedSkip = skip;
    runtimeChangedSkip["launch"]["runtime"]["size"] = 0;
    runtimeChangedSkip["launch"]["translator"]["size"] = 0;
    json(shaderprebuild::root() / "skip.json", runtimeChangedSkip);
    assert(shaderprebuild::skipped(game, "vulkan"));
    assert(!shaderprebuild::identity_matches(runtimeChangedSkip, game, "vulkan"));
    assert(shaderprebuild::request_rebuild());
    assert(!shaderprebuild::skipped(game, "vulkan"));
    fs::remove(shaderprebuild::root() / "skip.json");
    assert(shaderprebuild::activate_pending(game, "vulkan"));
    assert(!shaderprebuild::finish_pending(false));
    assert(shaderprebuild::read(shaderprebuild::resultPath).get("status").string() == "failed");
    assert(shaderprebuild::read(shaderprebuild::root() / "active.json").get("status").string() == "warm_pending");
    assert(shaderprebuild::requested());
    auto newer = active; newer["generation"] = "generation-newer";
    json(shaderprebuild::root() / "active.json", newer);
    assert(!shaderprebuild::finish_pending(true));
    assert(shaderprebuild::read(shaderprebuild::root() / "active.json").get("generation").string() == "generation-newer");
    json(shaderprebuild::root() / "active.json", active);
    assert(shaderprebuild::finish_pending(true));
    assert(shaderprebuild::read(shaderprebuild::root() / "active.json").get("status").string() == "ready");
    assert(shaderprebuild::read(shaderprebuild::resultPath).get("status").string() == "ready");
    assert(!shaderprebuild::requested());
    // Exercise the explicit Metal source on every host, not only macOS.
    const auto isolated = temporary/"explicit-migration";
    env("WWHD_USER_DIR",isolated.string().c_str());
    shaderprebuild::migrate_default_caches(isolated,metal);
    assert(bytes(shaderprebuild::root()/"live/shaders.bin")=="legacy Metal bytes");
    assert(bytes(metal)=="legacy Metal bytes");
    // The historical portable layout used a prefixed pipeline filename and
    // config/shaders.bin. Its local Metal cache takes precedence over HOME.
    const auto portable = temporary/"portable-migration";
    env("WWHD_USER_DIR",portable.string().c_str());
    write(portable/"shaders.bin","portable Metal bytes");
    write(portable/("shaders.bin."+pipeline),"portable pipeline bytes");
    write(portable/("shaders.bin."+pipeline+".recipes"),"portable recipe bytes");
    write(portable/"shaders.bin.vulkan-invalid.bin","unrelated portable bytes");
    shaderprebuild::migrate_default_caches(portable,metal);
    const auto portableLive=shaderprebuild::root()/"live";
    assert(bytes(portableLive/"shaders.bin")=="portable Metal bytes");
    assert(bytes(portableLive/pipeline)=="portable pipeline bytes");
    assert(bytes(portableLive/(pipeline+".recipes"))=="portable recipe bytes");
    assert(!fs::exists(portableLive/"shaders.bin.vulkan-invalid.bin"));
    assert(!fs::exists(portableLive/"vulkan-invalid.bin"));
    assert(bytes(portable/"shaders.bin")=="portable Metal bytes");
    assert(bytes(portable/("shaders.bin."+pipeline))=="portable pipeline bytes");
    assert(bytes(portable/("shaders.bin."+pipeline+".recipes"))=="portable recipe bytes");
    assert(bytes(metal)=="legacy Metal bytes");
    // Existing live entries win over both historical portable and HOME sources.
    const auto alreadyLive=temporary/"portable-existing-live";
    env("WWHD_USER_DIR",alreadyLive.string().c_str());
    write(alreadyLive/"shaders.bin","older portable bytes");
    write(alreadyLive/("shaders.bin."+pipeline),"older portable pipeline bytes");
    write(shaderprebuild::root()/"live/shaders.bin","current live Metal bytes");
    write(shaderprebuild::root()/"live"/pipeline,"current live pipeline bytes");
    shaderprebuild::migrate_default_caches(alreadyLive,metal);
    assert(bytes(shaderprebuild::root()/"live/shaders.bin")=="current live Metal bytes");
    assert(bytes(shaderprebuild::root()/"live"/pipeline)=="current live pipeline bytes");
    // Deterministically exercise the hard-link/no-replace-rename fallback.
    const auto fallback=temporary/"fallback-migration";
    env("WWHD_USER_DIR",fallback.string().c_str());
    write(fallback/"shadercache"/pipeline,"fallback pipeline bytes");
    shaderprebuild::migrate_default_caches(fallback,metal,shaderprebuild::LegacyPublishMode::ExclusiveCopy);
    assert(bytes(shaderprebuild::root()/"live"/pipeline)=="fallback pipeline bytes");
    assert(fs::is_regular_file(shaderprebuild::root()/"live/.legacy-defaults-v1"));
    assert(bytes(fallback/"shadercache"/pipeline)=="fallback pipeline bytes");
    const auto staged=temporary/"fallback-staged", existing=temporary/"fallback-existing";
    write(staged,"replacement must not win"); write(existing,"existing wins");
    assert(!shaderprebuild::publish_legacy_file(staged,existing,shaderprebuild::LegacyPublishMode::ExclusiveCopy));
    assert(bytes(existing)=="existing wins" && bytes(staged)=="replacement must not win");
    const auto failedTarget=temporary/"fallback-failed-output";
    assert(!shaderprebuild::publish_legacy_file(temporary/"absent-staging",failedTarget,
                                               shaderprebuild::LegacyPublishMode::ExclusiveCopy));
    assert(!fs::exists(failedTarget)); // An owned partial target is cleaned up.
    const auto replacedTarget=temporary/"fallback-replaced-output";
    write(fs::path(replacedTarget.string()+".replacement"),"concurrent renderer cache");
    auto replaceBeforeCleanup=[](const fs::path& target) {
        assert(host::replace_file(target.string()+".replacement",target.string()));
    };
    assert(!shaderprebuild::publish_legacy_file(temporary/"absent-staging",replacedTarget,
        shaderprebuild::LegacyPublishMode::ExclusiveCopy,replaceBeforeCleanup));
    assert(bytes(replacedTarget)=="concurrent renderer cache");
    // Publication failure must leave migration retryable, not marked complete.
    const auto retry=temporary/"retry-migration";
    env("WWHD_USER_DIR",retry.string().c_str());
    write(retry/"shadercache"/pipeline,"retry pipeline bytes");
    shaderprebuild::migrate_default_caches(retry,fs::path{},shaderprebuild::LegacyPublishMode::Fail);
    assert(!fs::exists(shaderprebuild::root()/"live/.legacy-defaults-v1"));
    assert(!fs::exists(shaderprebuild::root()/"live"/pipeline));
    shaderprebuild::migrate_default_caches(retry,fs::path{},shaderprebuild::LegacyPublishMode::ExclusiveCopy);
    assert(bytes(shaderprebuild::root()/"live"/pipeline)=="retry pipeline bytes");
    assert(fs::is_regular_file(shaderprebuild::root()/"live/.legacy-defaults-v1"));
    // A regular file blocking the destination directory makes copying fail
    // deterministically even when tests run with elevated permissions.
    const auto failure = temporary/"failed-migration";
    env("WWHD_USER_DIR",failure.string().c_str());
    write(failure/"shadercache"/pipeline,"source must survive copy failure");
    write(shaderprebuild::root()/"live","existing destination must survive");
    shaderprebuild::migrate_default_caches(failure,fs::path{});
    assert(bytes(failure/"shadercache"/pipeline)=="source must survive copy failure");
    assert(bytes(shaderprebuild::root()/"live")=="existing destination must survive");
    assert(!fs::exists(shaderprebuild::root()/"live/.legacy-defaults-v1"));
    // Activation intentionally keeps this process-wide lease alive during play.
    // End that lifetime before deleting the synthetic installation on Windows.
    shaderprebuild::generationLease.reset();
    const auto leasedGeneration = shaderprebuild::root()/"generation-lease-test";
    write(leasedGeneration/"manifest.json", "synthetic generation");
    shaderprebuild::GenerationLease lease;
    assert(lease.acquire(leasedGeneration));
    shaderprebuild::cleanup_generations({});
    assert(fs::exists(leasedGeneration)); // An active reader prevents deletion.
    lease.release();
    lease.release(); // Deterministic and idempotent, before the destructor.
    shaderprebuild::cleanup_generations({});
    if (fs::exists(leasedGeneration))
        std::fprintf(stderr, "Released generation still exists: [%s]\n", leasedGeneration.string().c_str());
    assert(!fs::exists(leasedGeneration));
    std::error_code cleanupError;
    fs::remove_all(testRoot, cleanupError);
    if (cleanupError) {
        std::fprintf(stderr, "Cleanup failed for [%s]: %s\n", testRoot.string().c_str(), cleanupError.message().c_str());
        std::error_code scanError;
        for (fs::recursive_directory_iterator it(testRoot, scanError), end; !scanError && it != end; it.increment(scanError)) {
            std::error_code itemError;
            if (!it->is_regular_file(itemError)) continue;
            fs::remove(it->path(), itemError);
            if (itemError) std::fprintf(stderr, "Cannot remove file [%s]: %s\n", it->path().string().c_str(), itemError.message().c_str());
        }
        return 1;
    }
}
