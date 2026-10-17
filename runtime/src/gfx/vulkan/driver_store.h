#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace gfxvk::drivers {
struct DeviceLoss { std::string driver; bool custom = false; };
struct Driver {
    std::string id, name, version, library;
    int min_api = 0;
    std::filesystem::path directory;
};
// No Vulkan or Android dependency: package and probe state are host-testable.
class Store {
    std::filesystem::path root_;
    std::string selected_, probing_;
    unsigned frames_ = 0;
    void write(const std::filesystem::path&, const std::string&);
public:
    explicit Store(std::filesystem::path root);
    std::vector<Driver> list() const;
    Driver read(const std::filesystem::path&) const;
    std::string install(const std::filesystem::path& zip, int api);
    void select(const std::string& id);
    void remove(const std::string& id);
    // Called once at process start, BEFORE touching the custom driver.
    // Returns true if an incomplete probe forced system fallback.
    bool start();
    bool recover_incomplete_probe(); // startup recovery before an explicit driver choice
    void rendered_frame();
    void failed();
    // Private bounded metadata only; no guest memory, save data, or package paths.
    void note_device_lost(const std::string& actual_driver, bool custom);
    std::optional<DeviceLoss> take_device_loss();
    std::string selected() const { return selected_; }
    std::filesystem::path cache(const std::string& id) const;
};
}
