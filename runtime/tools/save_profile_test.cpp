#include "mods/save_folder.h"
#include "mods/save_profile_state.h"
#include "full_state_header.h"
#include <cassert>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
namespace profile = mods::save_profile;
static void write(const fs::path &p, const std::string &value) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << value;
}
static std::string read(const fs::path &p) {
    std::ifstream file(p);
    return {std::istreambuf_iterator<char>(file), {}};
}
int main() {
    auto manifest = mods::json::parse(
        R"({"save_profile":{"version":1,"provider":"test","identity":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}})");
    const auto declaration = profile::parse(manifest, true, true);
    assert(declaration.error.empty());
    assert(!profile::parse(manifest, true, false).error.empty());
    assert(!profile::parse(manifest, false, true).error.empty());
    auto malformed = manifest;
    malformed["save_profile"]["extra"] = true;
    assert(!profile::parse(malformed, true, true).error.empty());
    auto selected = profile::select({{"owner", "Owner", std::string(64, 'b'), declaration}});
    assert(profile::valid_namespace(selected.namespace_key()));
    bool rejected = false;
    try {
        profile::select({{"owner", "Owner", std::string(64, 'b'), declaration},
                         {"other", "Other", std::string(64, 'c'), declaration}});
    } catch (const profile::Error &) {
        rejected = true;
    }
    assert(rejected);
    const auto root =
        fs::temp_directory_path() / ("wwhd-save-profile-" + std::to_string(save_folder::now()));
    fs::remove_all(root);
    const auto normal = root / "data/save", config = root / "config";
    auto paths = profile::paths(selected, normal, config, false);
    assert(paths.save.parent_path() == normal.parent_path());
    assert(paths.backups.parent_path() == normal.parent_path());
    assert(!profile::contains_path(normal, paths.save));
    const fs::path exact = root / "custom/../explicit";
    assert(profile::paths(selected, exact, config, true).save ==
           profile::sibling(exact, selected.namespace_key()));
    // Official launchers pass --save <data>/save. Both paths must stay isolated.
    const auto launched = profile::paths(selected, normal, config, true);
    assert(launched.save == paths.save && launched.backups == paths.backups);
    assert(launched.save != normal);
    // --save naming the profile's own folder again maps to the same folders, not a nested save-<ns>-<ns>
    const auto again = profile::paths(selected, paths.save, config, true);
    assert(again.save == paths.save && again.backups == paths.backups &&
           again.protected_roots == paths.protected_roots);
    assert(profile::paths({}, exact, config, true).save == exact);
    assert(profile::paths({}, normal, config, false).save == normal);
    assert(profile::windows_budget(211, 48) && !profile::windows_budget(212, 48));
    write(normal / "user/cking.sav", "vanilla");
    write(paths.save / "user/cking.sav", "seed");
    write(root / "data/save-backups/keep", "normal backup");
    // Launcher --save must never make a profile backup/restore touch vanilla.
    mods::saves::Store launcher_backup(launched.save, launched.backups);
    assert(launcher_backup.create("manual").ok);
    assert(launcher_backup.list().size() == 1);
    const auto launcher_snapshot = launcher_backup.list().front().name;
    write(launched.save / "user/cking.sav", "changed seed");
    std::string launcher_restore_error;
    assert(launcher_backup.restore(launcher_snapshot, launcher_restore_error));
    assert(launcher_backup.apply_pending().ok);
    assert(read(launched.save / "user/cking.sav") == "seed");
    assert(read(normal / "user/cking.sav") == "vanilla");
    assert(read(root / "data/save-backups/keep") == "normal backup");
    save_folder::Policy policy{
        selected.namespace_key(), paths.backups, paths.protected_roots, {normal}, false};
    auto start = save_folder::startup(paths.save, paths.preferences, {}, {}, policy);
    assert(!start.quit && start.save_dir == paths.save);
    const auto cloud = root / "cloud";
    assert(save_folder::choose(cloud.string()));
    auto choice = save_folder::read_choice(save_folder::choice_file(paths.preferences));
    assert(choice && profile::contains_path(fs::absolute(cloud), choice->folder));
    start = save_folder::startup(paths.save, paths.preferences, {}, {}, policy);
    assert(!start.quit);
    assert(read(start.save_dir / "user/cking.sav") == "seed");
    assert(read(normal / "user/cking.sav") == "vanilla");
    assert(read(root / "data/save-backups/keep") == "normal backup");
    // Picking an actual vanilla save creates a sibling, not a nested save.
    const auto actual = root / "cloud-actual";
    write(actual / "user/cking.sav", "cloud vanilla");
    assert(save_folder::choose(actual.string()));
    choice = save_folder::read_choice(save_folder::choice_file(paths.preferences));
    assert(choice && fs::path(choice->folder).parent_path() == actual.parent_path());
    assert(!profile::contains_path(actual, choice->folder));
    assert(read(actual / "user/cking.sav") == "cloud vanilla");
    // Ordinary backup/restore and cloud moves cannot contain or replace seed data.
    mods::saves::Store vanilla_backup(normal);
    assert(vanilla_backup.create("manual").ok);
    assert(vanilla_backup.list().size() == 1);
    assert(vanilla_backup.list().front().files == 1);
    const auto snapshot = vanilla_backup.list().front().name;
    write(normal / "user/cking.sav", "changed vanilla");
    std::string restore_error;
    assert(vanilla_backup.restore(snapshot, restore_error));
    assert(vanilla_backup.apply_pending().ok);
    assert(read(normal / "user/cking.sav") == "vanilla");
    assert(read(paths.save / "user/cking.sav") == "seed");
    const auto normal_cloud = root / "normal-cloud";
    const auto seed_cloud = profile::sibling(normal_cloud, selected.namespace_key());
    write(seed_cloud / "user/cking.sav", "existing cloud seed");
    auto normal_start = save_folder::startup(normal, config, {}, {});
    assert(!normal_start.quit);
    assert(save_folder::choose(normal_cloud.string()));
    normal_start = save_folder::startup(normal, config, {}, {});
    assert(!normal_start.quit);
    assert(read(normal_start.save_dir / "user/cking.sav") == "vanilla");
    assert(read(seed_cloud / "user/cking.sav") == "existing cloud seed");
    assert(read(paths.save / "user/cking.sav") == "seed");
    std::string why;
    assert(!profile::owned_location(normal / selected.namespace_key(), selected.namespace_key(),
                                    {normal}, why));
    const auto alias = root / ("alias-" + selected.namespace_key());
    std::error_code ec;
    fs::create_directory_symlink(normal, alias, ec);
    if (!ec)
        assert(!profile::owned_location(alias, selected.namespace_key(), {}, why));
    profile::StateIdentity identity{selected.namespace_key(), selected.owner, selected.fingerprint};
    assert(profile::portable_compatible(identity.namespace_key, identity.namespace_key));
    assert(!profile::portable_compatible("", identity.namespace_key));
    assert(!profile::portable_compatible("mod-fixture-" + std::string(64, 'c'), identity.namespace_key));
    assert(profile::portable_compatible("", ""));
    assert(profile::valid_state_requirement(ss::kRequiresSaveProfile, identity));
    assert(!profile::valid_state_requirement(0, identity));
    assert(!profile::valid_state_requirement(ss::kRequiresSaveProfile, {}));
    assert(profile::valid_state_requirement(0, {}));
    ss::Writer writer;
    profile::write_state_identity(writer, identity);
    assert(profile::compatible_full_state(ss::Reader(writer.b.data(), writer.b.size()), identity));
    auto changed = identity;
    changed.fingerprint = std::string(64, 'd');
    assert(!profile::compatible_full_state(ss::Reader(writer.b.data(), writer.b.size()), changed));
    assert(!profile::compatible_full_state(ss::Reader(nullptr, 0), identity));
    assert(profile::compatible_full_state(ss::Reader(nullptr, 0), {}));
    ss::Reader invalid;
    invalid.ok = false;
    assert(!profile::compatible_full_state(invalid, {}));
    writer.b.push_back(0);
    assert(!profile::compatible_full_state(ss::Reader(writer.b.data(), writer.b.size()), identity));
    for (uint32_t flags : {0u, ss::kRequiresSaveProfile, 2u}) {
        ss::FullStateHeader header{};
        memcpy(header.magic, "WWHDSTAT", 8);
        header.header_size = sizeof header;
        header.reserved = flags;
        FILE *file = tmpfile();
        assert(file);
        assert(fwrite(&header, sizeof header, 1, file) == 1);
        rewind(file);
        ss::FullStateHeader loaded{};
        assert(ss::read_full_state_header(file, loaded, why) == (flags < 2));
        // Exact pre-feature predicate: the required-feature header is rejected.
        assert((loaded.controller <= 2 && !loaded.reserved) == (flags == 0));
        fclose(file);
    }
    fs::remove_all(root);
    std::cout << "save profiles: strict declaration, filesystem isolation, cloud containers and "
                 "state identity pass\n";
}
