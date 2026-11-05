// SPDX-License-Identifier: MPL-2.0
// A randomized game's spoiler log (TWWHD Randomizer 1.2.0's "<hash> Spoiler Log.txt") and the spoiler map's places
// (setup's randomizer/places.tsv, tools/rando/places.py), for Settings > Randomizer. Parsing only: no files, no UI.
// After GreenNaugahyde/ZeldaWWHDRecompAndroid's SpoilerLog.java (MPL-2.0, db65d60).
#pragma once
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rando_spoiler {

struct Entry {
    std::string location, item;
    int sphere = -1;  // the playthrough's step that needs it (-1: not on the way)
};

struct Log {
    std::string permalink, version, starting_island;
    std::vector<std::string> starting_items;
    std::vector<std::vector<Entry>> spheres;  // the playthrough
    std::vector<Entry> all;                    // every location
    std::vector<std::pair<std::string, std::string>> entrances;  // "A -> B", "C from D"
    std::vector<std::pair<std::string, std::string>> hints;      // who, what
    std::vector<std::pair<std::string, std::string>> charts;     // chart, island
};

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

inline int indent(const std::string& s) {
    int n = 0;
    for (char c : s) {
        if (c == ' ') n++;
        else if (c == '\t') n += 4;
        else break;
    }
    return n;
}

// "Name:   value" -> (Name, value); a location name has ": " only at its end in these logs
inline bool split_pair(const std::string& line, std::string& a, std::string& b) {
    const std::string t = trim(line);
    size_t c = t.find(":\t");
    if (c == std::string::npos) c = t.find(": ");
    if (c == std::string::npos) return false;
    a = trim(t.substr(0, c));
    b = trim(t.substr(c + 1));
    return !a.empty() && !b.empty();
}

inline bool parse(const std::string& text, Log& out) {
    out = Log{};
    std::istringstream in(text);
    std::string line, section, hint_who;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trim(line).empty()) continue;
        if (line[0] == '#') {
            const std::string v = "# Wind Waker HD Randomizer Version ";
            if (line.compare(0, v.size(), v) == 0) out.version = trim(line.substr(v.size()));
            continue;
        }
        if (indent(line) == 0) {
            std::string a, b;
            if (line.back() == ':') {
                section = line.substr(0, line.size() - 1);
                continue;
            }
            section.clear();
            if (split_pair(line, a, b)) {
                if (a == "Permalink") out.permalink = b;
                else if (a == "Starting Island") out.starting_island = b;
            }
            continue;
        }
        std::string a, b;
        if (section == "Starting Inventory") {
            out.starting_items.push_back(trim(line));
        } else if (section == "Playthrough") {
            const std::string t = trim(line);
            if (indent(line) <= 4 && t.compare(0, 7, "Sphere ") == 0) out.spheres.emplace_back();
            else if (split_pair(line, a, b) && !out.spheres.empty()) {
                out.spheres.back().push_back({a, b, int(out.spheres.size()) - 1});
            }
        } else if (section == "All Locations") {
            if (split_pair(line, a, b)) out.all.push_back({a, b, -1});
        } else if (section.compare(0, 14, "Entrances for ") == 0) {
            if (split_pair(line, a, b)) out.entrances.emplace_back(a, b);
        } else if (section == "Hints") {
            if (indent(line) <= 4 && trim(line).back() == ':') hint_who = trim(line).substr(0, trim(line).size() - 1);
            else out.hints.emplace_back(hint_who, trim(line));
        } else if (section.compare(0, 11, "Charts for ") == 0) {
            if (split_pair(line, a, b)) out.charts.emplace_back(a, b);
        }
    }
    // each location's playthrough step
    std::map<std::string, int> step;
    for (const auto& s : out.spheres)
        for (const auto& e : s) step.emplace(e.location, e.sphere);
    for (auto& e : out.all)
        if (auto it = step.find(e.location); it != step.end()) e.sphere = it->second;
    return !out.all.empty();
}

// ---- the spoiler map's places
struct Place {
    int square = 0;     // 1..49 of the sea chart (row by row from the north-west), 0: none
    std::string stage;  // "" the sea
    float x = NAN, y = NAN, z = NAN;
    bool spot() const { return !std::isnan(x) && !std::isnan(z); }
};

inline std::map<std::string, Place> parse_places(const std::string& tsv) {
    std::map<std::string, Place> r;
    std::istringstream in(tsv);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        std::string cell;
        std::istringstream ls(line);
        while (std::getline(ls, cell, '\t')) f.push_back(cell);
        if (f.size() < 6) continue;
        Place p;
        p.square = std::atoi(f[1].c_str());
        if (p.square < 0 || p.square > 49) p.square = 0;
        p.stage = f[2] == "-" ? std::string() : f[2];
        auto num = [](const std::string& s) { return s == "nan" ? NAN : std::strtof(s.c_str(), nullptr); };
        p.x = num(f[3]), p.y = num(f[4]), p.z = num(f[5]);
        r[f[0]] = p;
    }
    return r;
}

// the island (or place) a location belongs to: its name before " - "
inline std::string area(const std::string& location) {
    const size_t d = location.find(" - ");
    return d == std::string::npos ? location : location.substr(0, d);
}

// a square's name: the area most of its places belong to
inline std::map<int, std::string> square_names(const std::map<std::string, Place>& places) {
    std::map<int, std::map<std::string, int>> count;
    for (const auto& [name, p] : places)
        if (p.square > 0 && p.stage.empty()) count[p.square][area(name)]++;
    std::map<int, std::string> r;
    for (const auto& [sq, names] : count) {
        int best = 0;
        for (const auto& [n, c] : names)
            if (c > best) best = c, r[sq] = n;
    }
    return r;
}

}  // namespace rando_spoiler
