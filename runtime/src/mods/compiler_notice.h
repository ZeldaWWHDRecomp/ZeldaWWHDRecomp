#pragma once
// Code mods (guest packages: the Story Notebook's quests, signs and letters, ...) are built with the
// compiler setup downloads into the data folder (toolchain/) and its Python. When the compiler is gone
// (deleted, or setup's "remove the downloaded compiler" answered yes) those mods can't start (issue #266):
// the Mods tab shows kCompilerMissing next to each of them (BuildBridge::installed raises it), the log
// and the screen say once per start which mods it stops (compiler_notice_for). Plain words: players
// read these.
#include <string>
#include <vector>

namespace guestmods {
inline constexpr const char* kCompilerMissing="The mod compiler is missing. Run setup and choose Repair to install it again.";
inline constexpr const char* kPythonMissing="The Python that setup installs for mods is missing. Run setup and choose Repair to install it again.";

// "<mod> needs the mod compiler, which is missing. ..." for the mods (names) it stops; empty for none
inline std::string compiler_notice(const std::vector<std::string>& names) {
    if(names.empty())return {};
    std::string who=names[0];
    if(names.size()==2)who+=" and "+names[1];
    else if(names.size()>2)who+=" and "+std::to_string(names.size()-1)+" other mods";
    return who+(names.size()==1?" needs":" need")+" the mod compiler, which is missing. Run setup and choose Repair to install it again.";
}

// The notice for the mod manager's list (mods::packages::list(): kind, enabled, active, reason, name, id):
// the code mods the player switched on that didn't start because the compiler is missing.
template<class Views> std::string compiler_notice_for(const Views& list) {
    std::vector<std::string> names;
    for(const auto& v:list)
        if(v.kind=="guest"&&v.enabled&&!v.active&&v.reason==kCompilerMissing)names.push_back(v.name.empty()?v.id:v.name);
    return compiler_notice(names);
}
}  // namespace guestmods
