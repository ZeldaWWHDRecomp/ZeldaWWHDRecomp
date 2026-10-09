// Read-only typed setting values, independent of guest memory and window hosts.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <variant>

namespace guestmods::settings {
// Stable wire types: UTF-8 including NUL, boolean u32, unsigned u32, IEEE double.
enum Type:uint32_t {Absent=0,String=1,Boolean=2,Unsigned=3,Number=4};
using Data=std::variant<std::string,bool,uint32_t,double>;
struct Value {
    Data data;
    uint64_t revision=1;
    Type type()const{return Type(data.index()+1);}
};
class Observed {
    std::mutex mutex;
    std::map<std::string,Value> values;
public:
    // Revisions are per key, starting at one. Unknown keys never allocate an entry.
    std::optional<Value> sample(const std::string& key,std::optional<Data> current) {
        if(!current)return {};
        std::lock_guard guard(mutex);
        auto [it,inserted]=values.try_emplace(key,Value{*current,1});
        if(!inserted&&it->second.data!=*current){it->second.data=*current;++it->second.revision;}
        return it->second;
    }
};
std::optional<Value> read(const std::string& key);
} // namespace guestmods::settings
