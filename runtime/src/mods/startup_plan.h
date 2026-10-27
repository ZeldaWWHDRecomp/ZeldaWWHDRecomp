#pragma once
// Which of the enabled mods start, and in what order. A mod whose own requirement can't be met (the
// GameCube game for mods is gone, a dependency is missing or too old, its manifest is broken) is skipped
// on its own, with a message that says what is missing and how to fix it; the mods that need it are
// skipped with it, naming it; every other enabled mod starts. Before this, the first such mod threw out of
// the dependency sort and no mod started at all (one missing .rvz stopped the Story Notebook, the sky
// island and every other code mod). Plain words: players read these messages in the log, in Settings →
// Mods and over the game.
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mods::startup_plan {

struct Mod {
    std::string name;                       // shown to the player
    std::string problem;                    // its own unmet requirement, a full sentence; empty: none
    std::vector<std::string> dependencies;  // package IDs (built-ins are checked into `problem`)
};
struct Plan {
    std::vector<std::string> order;             // startable mods, each after its dependencies
    std::map<std::string, std::string> skipped;  // enabled mods that don't start, with the reason
};

// mods: every installed package by ID; enabled: the IDs the active profile switched on.
inline Plan plan(const std::map<std::string, Mod>& mods, const std::set<std::string>& enabled) {
    Plan result;
    enum class Mark { None, Visiting, Done };
    std::map<std::string, Mark> mark;
    std::vector<std::string> path;  // the dependency chain being visited, for a cycle's message
    auto name_of = [&](const std::string& id) {
        auto it = mods.find(id);
        return it == mods.end() || it->second.name.empty() ? id : it->second.name;
    };
    // returns whether `id` starts; records why not in result.skipped
    std::function<bool(const std::string&)> visit = [&](const std::string& id) -> bool {
        auto& m = mark[id];
        if (m == Mark::Done) return !result.skipped.contains(id);
        const std::string name = name_of(id);
        if (m == Mark::Visiting) {
            // every mod on the loop is skipped with the loop's message
            const auto loop = std::find(path.begin(), path.end(), id);
            std::string chain;
            for (auto step = loop; step != path.end(); ++step) chain += name_of(*step) + " → ";
            chain += name;
            for (auto step = loop; step != path.end(); ++step)
                result.skipped.emplace(*step, name_of(*step) + " can't start: its dependencies form a loop (" + chain +
                                                  "). Turn " + name_of(*step) + " off.");
            return false;
        }
        m = Mark::Visiting;
        path.push_back(id);
        bool ok = true;
        auto found = mods.find(id);
        if (found == mods.end()) {
            result.skipped[id] = name + " is not installed. Install it in Settings → Mods, or turn it off.";
            ok = false;
        } else if (!found->second.problem.empty()) {
            result.skipped[id] = found->second.problem;
            ok = false;
        } else {
            for (const auto& dep : found->second.dependencies) {
                const std::string dep_name = name_of(dep);
                if (!enabled.contains(dep)) {
                    result.skipped[id] = name + " needs " + dep_name + ", which is turned off. Turn it on in Settings → Mods, or turn " +
                                         name + " off.";
                    ok = false;
                    break;
                }
                if (!visit(dep)) {
                    if (!result.skipped.contains(id)) {  // a loop through this mod named it already
                        auto why = result.skipped.find(dep);
                        result.skipped[id] = name + " needs " + dep_name + ", which could not start" +
                                             (why == result.skipped.end() ? std::string(".") : ": " + why->second);
                    }
                    ok = false;
                    break;
                }
            }
        }
        path.pop_back();
        m = Mark::Done;
        if (ok && result.skipped.contains(id)) ok = false;  // part of a loop found further down
        if (ok) result.order.push_back(id);
        return ok;
    };
    for (const auto& id : enabled) visit(id);
    return result;
}

// One line per skipped mod, in a stable order: for the log and the notice over the game.
inline std::vector<std::string> messages(const Plan& plan) {
    std::vector<std::string> out;
    for (const auto& [id, why] : plan.skipped) out.push_back(why);
    return out;
}

}  // namespace mods::startup_plan
