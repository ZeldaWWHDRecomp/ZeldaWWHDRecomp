// Save-state mod identity metadata contains declarations only, never code or mod memory.
#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace guestmods {
struct ModIdentity {
    std::string id,version;
    bool operator==(const ModIdentity&) const = default;
};
inline bool valid_identity(const ModIdentity& mod) {
    if(mod.id.empty()||mod.id.size()>64||mod.id[0]=='.'||mod.version.empty()||mod.version.size()>64)return false;
    for(unsigned char c:mod.id)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
    for(unsigned char c:mod.version)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='+'))return false;
    return true;
}
inline bool different_mods(std::vector<ModIdentity> a,std::vector<ModIdentity> b) {
    auto less=[](const ModIdentity& x,const ModIdentity& y){return x.id==y.id?x.version<y.version:x.id<y.id;};
    std::sort(a.begin(),a.end(),less);std::sort(b.begin(),b.end(),less);return a!=b;
}
inline constexpr const char* kModWarning="Warning: guest mod set differs from this state; behavior may differ.";
} // namespace guestmods
