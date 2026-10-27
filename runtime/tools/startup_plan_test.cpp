// mods/startup_plan.h: one mod whose requirement can't be met is skipped on its own (with its dependents),
// every other enabled mod starts in dependency order.
#include "mods/startup_plan.h"
#include <cassert>
#include <cstdio>

using mods::startup_plan::Mod;
using mods::startup_plan::plan;

static size_t at(const std::vector<std::string>& order, const std::string& id) {
    for (size_t i = 0; i < order.size(); i++) if (order[i] == id) return i;
    return order.size();
}
static bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int main() {
    const std::string gc_problem =
        "GameCube sea minimap: the GameCube game for mods was not found at /gone.rvz. Choose it in Settings → Mods → "
        "GameCube game for mods, or turn the mod off.";
    // the play setup that failed: the minimap's GameCube game is gone, the notebook and the sky island are fine
    std::map<std::string, Mod> mods{
        {"gc-minimap", {"GameCube sea minimap", gc_problem, {}}},
        {"story-notebook", {"Story Notebook", "", {}}},
        {"sky-island", {"Sky island", "", {"story-notebook"}}},
        {"test", {"Test", "", {}}},
        {"dragon", {"Dragon", "", {"story-notebook"}}},
    };
    {
        auto p = plan(mods, {"gc-minimap", "story-notebook", "sky-island", "test"});
        assert(p.skipped.size() == 1 && p.skipped.at("gc-minimap") == gc_problem);
        assert(p.order.size() == 3 && at(p.order, "story-notebook") < at(p.order, "sky-island") && at(p.order, "test") < 3);
        assert(mods::startup_plan::messages(p) == std::vector<std::string>{gc_problem});
    }
    {  // the minimap off: everything else starts, nothing is skipped
        auto p = plan(mods, {"story-notebook", "sky-island", "test"});
        assert(p.skipped.empty() && p.order.size() == 3);
    }
    {  // the requirement met: the minimap starts too
        auto met = mods;
        met.at("gc-minimap").problem.clear();
        auto p = plan(met, {"gc-minimap", "story-notebook", "sky-island", "test"});
        assert(p.skipped.empty() && p.order.size() == 4 && at(p.order, "gc-minimap") < 4);
    }
    {  // a skipped mod takes its dependents with it, naming the reason, and only them
        auto broken = mods;
        broken.at("story-notebook").problem = "Story Notebook: its manifest is broken.";
        broken["addon"] = {"Minimap addon", "", {"gc-minimap"}};
        broken["addon2"] = {"Addon of addon", "", {"addon"}};
        auto p = plan(broken, {"gc-minimap", "story-notebook", "sky-island", "dragon", "test", "addon", "addon2"});
        assert(p.order == std::vector<std::string>{"test"});
        assert(p.skipped.size() == 6);
        assert(p.skipped.at("sky-island") ==
               "Sky island needs Story Notebook, which could not start: Story Notebook: its manifest is broken.");
        assert(has(p.skipped.at("dragon"), "Dragon needs Story Notebook, which could not start"));
        assert(has(p.skipped.at("addon"), "Minimap addon needs GameCube sea minimap, which could not start: " + gc_problem));
        assert(has(p.skipped.at("addon2"), "Addon of addon needs Minimap addon, which could not start: Minimap addon needs"));
    }
    {  // a dependency switched off (an old profile): only its dependent is skipped
        auto p = plan(mods, {"sky-island", "test"});
        assert(p.order == std::vector<std::string>{"test"});
        assert(p.skipped.at("sky-island") ==
               "Sky island needs Story Notebook, which is turned off. Turn it on in Settings → Mods, or turn Sky island off.");
    }
    {  // an enabled ID that isn't installed (a folder removed by hand)
        auto p = plan(mods, {"gone", "test"});
        assert(p.order == std::vector<std::string>{"test"});
        assert(p.skipped.at("gone") == "gone is not installed. Install it in Settings → Mods, or turn it off.");
    }
    {  // a dependency loop: its members are skipped, a mod depending on it too, the rest starts
        std::map<std::string, Mod> loop{
            {"a", {"A", "", {"b"}}}, {"b", {"B", "", {"a"}}}, {"c", {"C", "", {"a"}}}, {"d", {"D", "", {}}}};
        auto p = plan(loop, {"a", "b", "c", "d"});
        assert(p.order == std::vector<std::string>{"d"});
        assert(has(p.skipped.at("a"), "A can't start: its dependencies form a loop (A → B → A)"));
        assert(has(p.skipped.at("b"), "B can't start: its dependencies form a loop"));
        assert(has(p.skipped.at("c"), "C needs A, which could not start: A can't start"));
    }
    {  // shared dependency visited once; order keeps every dependency first
        std::map<std::string, Mod> diamond{
            {"base", {"Base", "", {}}}, {"l", {"L", "", {"base"}}}, {"r", {"R", "", {"base"}}}, {"top", {"Top", "", {"l", "r"}}}};
        auto p = plan(diamond, {"top", "l", "r", "base"});
        assert(p.skipped.empty() && p.order.size() == 4);
        assert(at(p.order, "base") < at(p.order, "l") && at(p.order, "base") < at(p.order, "r"));
        assert(at(p.order, "l") < at(p.order, "top") && at(p.order, "r") < at(p.order, "top"));
    }
    std::puts("startup plan: a mod with an unmet requirement is skipped alone with its dependents; the rest start");
    return 0;
}
