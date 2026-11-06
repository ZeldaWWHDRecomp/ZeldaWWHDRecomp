// SPDX-License-Identifier: MPL-2.0
// randomizer_spoiler.h on a spoiler log in TWWHD Randomizer 1.2.0's layout and a places table (made-up entries).
#include "randomizer_spoiler.h"
#include <cassert>
#include <cmath>

static const char* kLog =
    "# Wind Waker HD Randomizer Version 1.2.0\n"
    "# Program opened Thu Nov  5 02:46:01 2026\n"
    "\n"
    "# Settings\n"
    "Permalink: ABC=\n"
    "program_version: 1.2.0\n"
    "starting_gear:\n"
    "  - Progressive Shield\n"
    "Starting Island: Birds Peak Rock\n"
    "\n"
    "Starting Inventory:\n"
    "    Progressive Shield\n"
    "    Ballad of Gales\n"
    "\n"
    "Playthrough:\n"
    "    Sphere 0:\n"
    "        Great Sea - Salvage Corp Gift:                           Grappling Hook\n"
    "        Outset Island - Great Fairy:                             Boomerang\n"
    "    Sphere 1:\n"
    "        Dragon Roost Cavern - Mini Boss:                         Dragon Roost Cavern Small Key\n"
    "\n"
    "Entrance Playthrough:\n"
    "    Sphere 0:\n"
    "        Outset Island -> Outset Cave: Somewhere\n"
    "\n"
    "Entrances for world 1:\n"
    "    Angular Isles Cave -> Angular Isles Small Isle:   ET First Room from Headstone Island Interior\n"
    "\n"
    "All Locations:\n"
    "    Outset Island - Great Fairy:                             Boomerang\n"
    "    Windfall Island - Jail Maze Chest:                       Red Rupee\n"
    "    Dragon Roost Cavern - Mini Boss:                         Dragon Roost Cavern Small Key\n"
    "\n"
    "Hints:\n"
    "    Big Octo Great Fairy:\n"
    "        They say that a magic leaf can be found at Windfall Island.\n"
    "\n"
    "Charts for world 1:\n"
    "    Triforce Chart 1:\tGreatfish Isle\n";

int main() {
    rando_spoiler::Log log;
    assert(rando_spoiler::parse(kLog, log));
    assert(log.version == "1.2.0" && log.permalink == "ABC=" && log.starting_island == "Birds Peak Rock");
    assert((log.starting_items == std::vector<std::string>{"Progressive Shield", "Ballad of Gales"}));
    assert(log.spheres.size() == 2 && log.spheres[0].size() == 2 && log.spheres[1].size() == 1);
    assert(log.spheres[0][1].location == "Outset Island - Great Fairy" && log.spheres[0][1].item == "Boomerang");
    assert(log.all.size() == 3);
    assert(log.all[0].sphere == 0 && log.all[1].sphere == -1 && log.all[2].sphere == 1);
    assert(log.all[1].location == "Windfall Island - Jail Maze Chest" && log.all[1].item == "Red Rupee");
    assert(log.entrances.size() == 1 && log.entrances[0].first == "Angular Isles Cave -> Angular Isles Small Isle");
    assert(log.entrances[0].second == "ET First Room from Headstone Island Interior");
    assert(log.hints.size() == 1 && log.hints[0].first == "Big Octo Great Fairy");
    assert(log.charts.size() == 1 && log.charts[0].first == "Triforce Chart 1" && log.charts[0].second == "Greatfish Isle");
    assert(!rando_spoiler::parse("nothing here\n", log));

    const auto places = rando_spoiler::parse_places(
        "# header\n"
        "Windfall Island - Jail Maze Chest\t11\t-\t-150000\t10\t250001\n"
        "Windfall Island - Letter\t11\t-\tnan\tnan\tnan\n"
        "Dragon Roost Cavern - Mini Boss\t13\tM_NewD2\t5\t6\t7\n"
        "Broken - Row\t99\t-\t1\n");
    assert(places.size() == 3);
    const auto& jail = places.at("Windfall Island - Jail Maze Chest");
    assert(jail.square == 11 && jail.stage.empty() && jail.spot() && std::fabs(jail.x + 150000) < 1);
    assert(!places.at("Windfall Island - Letter").spot());
    assert(places.at("Dragon Roost Cavern - Mini Boss").stage == "M_NewD2");
    const auto names = rando_spoiler::square_names(places);
    assert(names.at(11) == "Windfall Island" && !names.count(13));
    assert(rando_spoiler::area("Windfall Island - Jail Maze Chest") == "Windfall Island");
}
