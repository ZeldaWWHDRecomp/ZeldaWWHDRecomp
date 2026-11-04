#include "mods/build_constraint.h"
#include <cassert>
#include <iostream>
// Strong compiled identity, as generated table.c supplies in a real EUR build.
extern "C" const char g_guest_build_name[] = "EUR";
int main() {
    using mods::json::parse;
    using mods::packages::game_build_constraint_problem;
    assert(game_build_constraint_problem(parse("{}"), false).empty());
    const auto usa = parse(R"({"supported_game_builds":["USA"]})");
    const auto why = game_build_constraint_problem(usa, true);
    assert(why.find("USA") != std::string::npos && why.find("EUR") != std::string::npos);
    assert(game_build_constraint_problem(parse(R"({"supported_game_builds":["EUR"]})"), true).empty());
    assert(game_build_constraint_problem(parse(R"({"supported_game_builds":["USA","EUR"]})"), true).empty());
    for (const auto* text : {R"({"supported_game_builds":null})", R"({"supported_game_builds":[]})",
         R"({"supported_game_builds":["USA","USA"]})", R"({"supported_game_builds":["../USA"]})",
         R"({"supported_game_builds":[false]})"}) {
        bool rejected = false;
        try { game_build_constraint_problem(parse(text), true); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    bool rejected = false;
    try { game_build_constraint_problem(usa, false); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "linked EUR build refuses USA-only metadata; exact/multiple build and schema checks pass\n";
}
