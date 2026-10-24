// World editor command layer: quests, flags, search, copy and paste (commands.h, quests.h). No game
// dependencies: the loaded game's records, test mode and start-at-step go through Host.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../mods/story_mail_book.h"
#include "../mods/story_npc_catalogue.h"
#include "commands.h"
#include "quests.h"

namespace editor {
namespace {

std::string text(const Value& p, const char* key) { return p.get(key).string(); }
Value arr() {
    Value v;
    v.type = Value::Array;
    return v;
}
Value* by_id(Value& list, const std::string& id) {
    if (list.type != Value::Array) return nullptr;
    for (auto& v : list.array)
        if (v.get("id").type == Value::String && v.get("id").text == id) return &v;
    return nullptr;
}
const Value* by_id(const Value& list, const std::string& id) { return by_id(const_cast<Value&>(list), id); }
bool point_of(const Value& v, WorldPoint& p) {
    if (v.type != Value::Object) return false;
    for (const char* k : {"x", "y", "z"})
        if (v.get(k).type != Value::Number || !std::isfinite(v.get(k).number)) return false;
    p = {float(v.get("x").number), float(v.get("y").number), float(v.get("z").number)};
    return true;
}
double tenth(double v) { return std::round(v * 10) / 10; }
Value rounded(const WorldPoint& p) {
    Value v;
    v["x"] = tenth(p.x);
    v["y"] = tenth(p.y);
    v["z"] = tenth(p.z);
    return v;
}
unsigned step_n(const Value& s) {
    const Value& n = s.get("n");
    return n.type == Value::Number && std::floor(n.number) == n.number && n.number >= 1 && n.number <= 254 ? unsigned(n.number) : 0;
}
std::vector<unsigned> numbers_of(const Value& q) {
    std::vector<unsigned> out;
    for (const auto& s : q.get("steps").array)
        if (unsigned n = step_n(s)) out.push_back(n);
    std::sort(out.begin(), out.end());
    return out;
}
Value* step_of(Value& q, unsigned n) {
    for (auto& s : q["steps"].array)
        if (step_n(s) == n) return &s;
    return nullptr;
}
std::string slug(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() > 24) out.resize(24);
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("quest") : out;
}
std::vector<std::string> strings(const Value& list) {
    std::vector<std::string> out;
    for (const auto& v : list.array)
        if (v.type == Value::String) out.push_back(v.text);
    return out;
}
const std::set<std::string> kStepFields = {"text", "where", "place", "npc", "available", "sail_line", "find"};
const std::set<std::string> kQuestFields = {"title", "journal", "start", "reward", "letter"};

}  // namespace

int pick_villager(const Value& villagers, const WorldPoint& o, const WorldPoint& d, float range) {
    constexpr float kRadius = 70, kHeight = 160;
    int best = -1;
    float best_t = range;
    for (size_t i = 0; i < villagers.array.size(); i++) {
        WorldPoint at;
        if (!point_of(villagers.array[i].get("position"), at)) continue;
        // the ray's closest approach to the body's axis (a vertical segment), sampled along its height
        for (float h = 0; h <= kHeight; h += kHeight / 8) {
            const float cx = at.x - o.x, cy = at.y + h - o.y, cz = at.z - o.z;
            const float t = cx * d.x + cy * d.y + cz * d.z;
            if (t < 0 || t > best_t) continue;
            const float px = o.x + d.x * t - at.x, pz = o.z + d.z * t - at.z, py = o.y + d.y * t - (at.y + h);
            if (px * px + pz * pz <= kRadius * kRadius && std::fabs(py) <= kHeight / 8) {
                best_t = t;
                best = int(i);
                break;
            }
        }
    }
    return best;
}

bool Session::change_layout(Package& p, const std::string& label, const Value& layout, Value& result) {
    if (layout == p.layout) {
        result["changed"] = false;
        return true;
    }
    Step s{p.mod, label, p.layout, layout};
    s.layout = true;
    undo_.push_back(s);
    if (undo_.size() > kUndoSize) undo_.erase(undo_.begin());
    redo_.clear();
    p.layout = layout;
    result["changed"] = true;
    return true;
}

Value Session::dispatch_quest(const std::string& command, const Value& p, std::string& why, bool& ok, bool& handled) {
    Value r;
    ok = false;
    handled = true;
    const std::string quest = text(p, "quest");
    // a change to one quest of the open mod
    auto quest_change = [&](const std::string& label, const std::function<bool(Value& m, Value& q, std::string&)>& edit) {
        Package* pkg = package(p, why);
        if (!pkg) return;
        ok = change(*pkg, label, [&](Value& m, std::string& w) {
            Value* q = by_id(m["quests"], quest);
            if (!q) return w = "no quest \"" + quest + "\" in " + pkg->mod, false;
            return edit(m, *q, w);
        }, r, why);
        if (ok) r["quest"] = quest;
    };
    auto test_only = [&]() {
        if (!host_) return why = "the game is not running", false;
        if (!host_->test_mode()) return why = command + " works in test mode only (test_mode on)", false;
        return true;
    };

    // ---- the game's own villagers: list, pick, a quest talk (the villager itself is never changed)
    if (command == "list_villagers" || command == "pick_villager") {
        if (!host_) return why = "the game is not running", r;
        Value list;
        if (!host_->villagers(list, why)) return r;
        if (command == "list_villagers") {
            ok = true;
            return list;
        }
        WorldPoint origin, direction;
        if (!point_of(p.get("origin"), origin) || !point_of(p.get("direction"), direction)) return why = "origin and direction are points", r;
        const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (!(len > 1e-6f)) return why = "the direction has no length", r;
        direction = {direction.x / len, direction.y / len, direction.z / len};
        const float range = p.get("range").type == Value::Number ? float(p.get("range").number) : 20000.0f;
        const int i = pick_villager(list, origin, direction, range);
        if (i < 0) return why = "no villager the NPC catalogue knows there", r;
        ok = true;
        return list.array[size_t(i)];
    }
    if (command == "add_villager_talk") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string who = text(p, "who");
        const story_notebook::NpcCatalogue& catalogue = story_notebook::npc_catalogue();
        const story_notebook::NpcEntry* e = catalogue.entry(catalogue.find(who));
        if (!e) return why = "the NPC catalogue has no villager \"" + who + "\" (list_villagers)", r;
        if (!e->talks()) return why = e->name + " can't speak a quest's lines yet (only townspeople can)", r;
        Value lines = arr();
        if (p.get("lines").type == Value::String) lines.array.push_back(p.get("lines"));
        else if (p.get("lines").type == Value::Array) lines = p.get("lines");
        if (lines.array.empty() || lines.array.size() > 3) return why = "lines: one to three lines", r;
        for (const auto& l : lines.array)
            if (l.type != Value::String || l.text.empty()) return why = "lines: each line is text", r;
        Value npc;
        npc["who"] = who;
        npc["lines"] = lines;
        const std::string step_text = p.get("text").type == Value::String ? p.get("text").text : "Talk to " + e->display() + ".";
        std::string id = quest;
        const bool fresh = quest.empty();
        if (fresh) {
            if (text(p, "title").empty()) return why = "a new quest needs a title (or name an existing quest)", r;
            id = p.get("id").type == Value::String ? p.get("id").text : slug(text(p, "title"));
            if (p.get("id").type != Value::String && by_id(pkg->manifest.get("quests"), id))
                for (int n = 2;; n++)
                    if (!by_id(pkg->manifest.get("quests"), id + "-" + std::to_string(n))) {
                        id += "-" + std::to_string(n);
                        break;
                    }
            if (by_id(pkg->manifest.get("quests"), id)) return why = "the mod has a quest \"" + id + "\" already", r;
        } else if (p.get("title").type != Value::Null || p.get("id").type != Value::Null)
            return why = "title and id are for a new quest: leave out quest", r;
        unsigned at = 0;
        const std::string label = fresh ? "New quest " + id + " with a talk for " + who : "Quest " + id + ": a talk for " + who;
        ok = change(*pkg, label, [&](Value& m, std::string& w) {
            if (fresh) {
                if (m.get("quests").type != Value::Array) m["quests"] = arr();
                if (m.get("quests_format").type == Value::Null) m["quests_format"] = 1;
                Value q, step;
                q["id"] = id;
                q["title"] = text(p, "title");
                step["n"] = 1;
                step["text"] = step_text;
                step["npc"] = npc;
                q["steps"] = arr();
                q["steps"].array.push_back(step);
                m["quests"].array.push_back(q);
                at = 1;
                return true;
            }
            Value* q = by_id(m["quests"], id);
            if (!q) return w = "no quest \"" + id + "\" in " + pkg->mod, false;
            if (p.get("step").type == Value::Number) {
                at = unsigned(p.get("step").number);
                Value* s = step_of(*q, at);
                if (!s) return w = "quest " + id + " has no step " + std::to_string(at), false;
                (*s)["npc"] = npc;
                return true;
            }
            const auto numbers = numbers_of(*q);
            at = (numbers.empty() ? 0 : numbers.back()) + 1;
            if (at > 254) return w = "a quest has steps 1..254", false;
            Value step;
            step["n"] = int(at);
            step["text"] = step_text;
            step["npc"] = npc;
            (*q)["steps"].array.push_back(step);
            return true;
        }, r, why);
        if (ok) {
            r["quest"] = id;
            r["step"] = int(at);
            r["who"] = who;
            r["name"] = e->display();
        }
        return r;
    }
    if (command == "get_quest") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* q = by_id(pkg->manifest.get("quests"), quest);
        if (!q) return why = "no quest \"" + quest + "\"", r;
        ok = true;
        return *q;
    }
    if (command == "quest_graph") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (!quest_graph(pkg->mod, pkg->manifest, quest, r)) return why = "no quest \"" + quest + "\"", r;
        r["layout"] = pkg->layout.get("quests").get(quest);
        if (host_) {
            std::vector<int> values;
            std::string w;
            if (host_->read_records({{story_mail::kRecordQuest, quest_key(pkg->mod, quest)}}, values, w) && !values.empty()) r["saved_step"] = std::max(0, values[0]);
        }
        ok = true;
        return r;
    }
    if (command == "add_quest") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        std::string id = p.get("id").type == Value::String ? p.get("id").text : slug(text(p, "title"));
        if (p.get("id").type != Value::String && by_id(pkg->manifest.get("quests"), id))
            for (int n = 2;; n++)
                if (!by_id(pkg->manifest.get("quests"), id + "-" + std::to_string(n))) {
                    id += "-" + std::to_string(n);
                    break;
                }
        if (by_id(pkg->manifest.get("quests"), id)) return why = "the mod has a quest \"" + id + "\" already", r;
        ok = change(*pkg, "Add quest " + id, [&](Value& m, std::string&) {
            if (m.get("quests").type != Value::Array) m["quests"] = arr();
            if (m.get("quests_format").type == Value::Null) m["quests_format"] = 1;
            Value q, step;
            q["id"] = id;
            q["title"] = text(p, "title");
            step["n"] = 1;
            if (p.get("text").type == Value::String) step["text"] = p.get("text");
            q["steps"] = arr();
            q["steps"].array.push_back(step);
            m["quests"].array.push_back(q);
            return true;
        }, r, why);
        if (ok) r["quest"] = id;
        return r;
    }
    if (command == "remove_quest") {
        quest_change("Remove quest " + quest, [&](Value& m, Value&, std::string& w) {
            for (const auto& it : m.get("items").array)
                if (it.get("use").get("quest").string() == quest) return w = "item " + it.get("id").string() + " sets a step of " + quest + " when used: change the item first", false;
            for (const auto& l : m.get("letters").array)
                if (letter_waits_for(l, quest, 0)) return w = "letter " + l.get("id").string() + " waits for " + quest + " (its \"when\"): change the letter first", false;
            for (const auto& sc : m.get("scenes").array) {
                bool names = sc.get("trigger").get("quest").string() == quest;
                for (const auto& st : sc.get("steps").array) {
                    for (const auto& t : st.get("then").array)
                        names = names || ((t.get("action").string() == "quest_step" || t.get("action").string() == "quest_done") && t.get("id").string() == quest);
                    names = names || st.get("quest_place").string() == quest;
                }
                if (names) return w = "scene " + sc.get("id").string() + " names quest " + quest + ": change the scene first", false;
            }
            auto& list = m["quests"].array;
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Value& q) { return q.get("id").string() == quest; }), list.end());
            if (list.empty()) {
                m.object.erase("quests");
                m.object.erase("quests_format");
            }
            // every dialogue (the actor's own and its talks): conditions on the quest, actions on it
            auto clean = [&](Value& d) {
                if (d.get("when").get("quest").string() == quest) {
                    d["when"].object.erase("quest");
                    d["when"].object.erase("step");
                }
                if (d.get("when").get("quests").type == Value::Array) {
                    auto& qs = d["when"]["quests"].array;
                    qs.erase(std::remove_if(qs.begin(), qs.end(), [&](const Value& x) { return x.get("quest").string() == quest; }), qs.end());
                    if (qs.empty()) d["when"].object.erase("quests");
                }
                if (d.get("when").type == Value::Object && d.get("when").object.empty()) d.object.erase("when");
                if (d.get("then").type != Value::Array) return;
                auto& then = d["then"].array;
                then.erase(std::remove_if(then.begin(), then.end(),
                                          [&](const Value& t) {
                                              const std::string action = t.get("action").string();
                                              return (action == "quest_step" || action == "quest_done") && t.get("id").string() == quest;
                                          }),
                           then.end());
                if (then.empty()) d.object.erase("then");
            };
            if (m.get("actors").type == Value::Array)
                for (auto& a : m["actors"].array) {
                    clean(a);
                    if (a.get("talks").type == Value::Array)
                        for (auto& t : a["talks"].array) clean(t);
                }
            if (m.get("actors").type == Value::Array && m.get("actors").array.empty()) m.object.erase("actors");
            return true;
        });
        return r;
    }
    if (command == "set_quest") {
        const std::string property = text(p, "property");
        if (!kQuestFields.contains(property)) return why = "a quest's properties here are title, journal, start, reward and letter", r;
        if (property == "title" && p.get("value").type != Value::String) return why = "a quest needs a title", r;
        quest_change("Quest " + quest + ": " + property, [&](Value&, Value& q, std::string&) {
            if (p.get("value").type == Value::Null) q.object.erase(property);
            else q[property] = p.get("value");
            return true;
        });
        return r;
    }
    if (command == "add_quest_step" || command == "duplicate_quest_step") {
        unsigned added = 0;
        const bool dup = command == "duplicate_quest_step";
        quest_change(dup ? "Duplicate step " + std::to_string(int(p.get("step").number)) + " of " + quest : "Add a step to " + quest,
                     [&](Value& m, Value&, std::string& w) {
            Value* q = by_id(m["quests"], quest);
            const auto numbers = numbers_of(*q);
            const unsigned last = numbers.empty() ? 0 : numbers.back();
            Value step;
            if (dup) {
                Value* from = step_of(*q, unsigned(p.get("step").number));
                if (!from) return w = "quest " + quest + " has no step " + std::to_string(int(p.get("step").number)), false;
                step = *from;
                added = last + 1;
            } else {
                for (const char* k : {"text", "where", "place"})
                    if (p.get(k).type != Value::Null) step[k] = p.get(k);
                added = last + 1;
                if (p.get("after").type == Value::Number) {
                    const unsigned after = unsigned(p.get("after").number);
                    if (after && !step_of(*q, after)) return w = "quest " + quest + " has no step " + std::to_string(after), false;
                    // later steps move up by one
                    std::vector<std::pair<unsigned, unsigned>> map;
                    for (unsigned n : numbers)
                        if (n > after) map.push_back({n, n + 1});
                    if (!map.empty() && !renumber_steps(m, quest, map, w)) return false;
                    q = by_id(m["quests"], quest);
                    added = after + 1;
                }
            }
            if (added > 254) return w = "a quest has steps 1..254", false;
            step["n"] = int(added);
            (*q)["steps"].array.push_back(step);
            std::stable_sort((*q)["steps"].array.begin(), (*q)["steps"].array.end(), [](const Value& a, const Value& b) { return step_n(a) < step_n(b); });
            return true;
        });
        if (ok) r["step"] = int(added);
        return r;
    }
    if (command == "edit_quest_step") {
        const unsigned n = unsigned(p.get("step").number);
        for (const auto& [k, v] : p.get("fields").object)
            if (!kStepFields.contains(k)) return why = "\"" + k + "\" is not a step property (text, where, place, npc, available, sail_line, find)", r;
        quest_change("Quest " + quest + " step " + std::to_string(n), [&](Value&, Value& q, std::string& w) {
            Value* s = step_of(q, n);
            if (!s) return w = "quest " + quest + " has no step " + std::to_string(n), false;
            for (const auto& [k, v] : p.get("fields").object) {
                if (v.type == Value::Null) s->object.erase(k);
                else (*s)[k] = v;
            }
            return true;
        });
        if (ok) r["step"] = int(n);
        return r;
    }
    if (command == "remove_quest_step") {
        const unsigned n = unsigned(p.get("step").number);
        std::vector<std::string> changed;
        quest_change("Remove step " + std::to_string(n) + " of " + quest, [&](Value& m, Value& q, std::string& w) {
            if (numbers_of(q).size() <= 1) return w = "a quest keeps at least one step (remove_quest removes the quest)", false;
            return remove_step(m, quest, n, changed, w);
        });
        if (ok) {
            r["step"] = int(n);
            r["actors"] = arr();
            for (const auto& id : changed) r["actors"].array.push_back(Value(id));
        }
        return r;
    }
    if (command == "move_quest_step") {
        const unsigned n = unsigned(p.get("step").number);
        const int to = int(p.get("to").number);
        quest_change("Move step " + std::to_string(n) + " of " + quest, [&](Value& m, Value& q, std::string& w) {
            const auto numbers = numbers_of(q);
            auto order = numbers;
            const auto it = std::find(order.begin(), order.end(), n);
            if (it == order.end()) return w = "quest " + quest + " has no step " + std::to_string(n), false;
            if (to < 1 || to > int(order.size())) return w = "\"to\" is 1.." + std::to_string(order.size()), false;
            order.erase(it);
            order.insert(order.begin() + (to - 1), n);
            // the steps keep the numbers they have, in their new order
            std::vector<std::pair<unsigned, unsigned>> map;
            for (size_t i = 0; i < order.size(); i++)
                if (order[i] != numbers[i]) map.push_back({order[i], numbers[i]});
            return map.empty() || renumber_steps(m, quest, map, w);
        });
        return r;
    }
    if (command == "set_step_place") {
        const unsigned n = unsigned(p.get("step").number);
        const std::string from = text(p, "from");
        Value place, where;
        if (from == "link" || from == "camera") {
            if (!host_) return why = "the game is not running", r;
            const Place at = from == "link" ? host_->link() : host_->camera();
            if (!at.valid) return why = from == "link" ? "Link is not in the scene" : "the fly camera is off", r;
            if (at.stage != "sea")
                return why = "a step's place is a point on the sea chart: " + at.stage + " is not at sea (warp to the island first)", r;
            place["x"] = tenth(at.position.x);
            place["z"] = tenth(at.position.z);
            if (const char* name = island_name(sea_room(at.position.x, at.position.z))) where = Value(std::string(name));
        } else if (from == "location") {
            bool found = false;
            for (const auto& l : locations())
                if (l.id == text(p, "location")) {
                    found = true;
                    if (l.stage != "sea") return why = "a step's place is a point on the sea chart: " + l.label + " is not at sea", r;
                    const WorldPoint at = l.point == kAtPosition ? l.position : sea_room_centre(l.room);
                    place["x"] = tenth(at.x);
                    place["z"] = tenth(at.z);
                    if (const char* name = island_name(l.room)) where = Value(std::string(name));
                }
            if (!found) return why = "no location \"" + text(p, "location") + "\" (list_locations)", r;
        } else if (from != "none")
            return why = "\"from\" is link, camera, location or none", r;
        quest_change("Place of step " + std::to_string(n) + " of " + quest, [&](Value&, Value& q, std::string& w) {
            Value* s = step_of(q, n);
            if (!s) return w = "quest " + quest + " has no step " + std::to_string(n), false;
            if (from == "none") {
                s->object.erase("place");
                return true;
            }
            (*s)["place"] = place;
            if (where.type == Value::String) (*s)["where"] = where;
            return true;
        });
        if (ok && place.type == Value::Object) r["place"] = place;
        return r;
    }
    if (command == "connect_quest_steps" || command == "disconnect_quest_steps") {
        const unsigned from = unsigned(std::max(0.0, p.get("from").number)), to = unsigned(std::max(0.0, p.get("to").number));
        const std::string via = text(p, "via");
        const bool connect = command == "connect_quest_steps";
        quest_change((connect ? "Connect " : "Disconnect ") + (from ? "step " + std::to_string(from) : std::string("start")) + " -> step " + std::to_string(to) + " (" + via + ")",
                     [&](Value& m, Value&, std::string& w) {
                         return connect ? connect_steps(m, quest, from, to, via, text(p, "item"), text(p, "time"), w) : disconnect_steps(m, quest, from, to, via, w);
                     });
        return r;
    }
    if (command == "set_quest_layout") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (!by_id(pkg->manifest.get("quests"), quest)) return why = "no quest \"" + quest + "\"", r;
        Value layout = pkg->layout.type == Value::Object ? pkg->layout : Value();
        layout["format"] = 1;
        for (const auto& [node, at] : p.get("positions").object) {
            if (node != "start" && node != "end" && node.rfind("step:", 0) != 0) return why = "node ids are start, end and step:N", r;
            if (at.get("x").type != Value::Number || at.get("y").type != Value::Number || !std::isfinite(at.get("x").number) || !std::isfinite(at.get("y").number))
                return why = "a node's place is {x, y}", r;
            Value v;
            v["x"] = std::round(at.get("x").number);
            v["y"] = std::round(at.get("y").number);
            layout["quests"][quest][node] = v;
        }
        ok = change_layout(*pkg, "Arrange quest " + quest, layout, r);
        return r;
    }
    if (command == "start_at_step") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (!host_) return why = "the game is not running", r;
        StepStart s;
        if (!step_start(pkg->mod, pkg->manifest, quest, unsigned(p.get("step").number), text(p, "warp"), s, why)) return r;
        std::vector<Record> erase, shared;
        for (const auto& other : packages_)
            if (other.mod != pkg->mod)
                for (const auto& rec : mod_records(other.mod, other.manifest)) shared.push_back(rec);
        for (const auto& rec : mod_records(pkg->mod, pkg->manifest))
            if (std::find(shared.begin(), shared.end(), rec) == shared.end()) erase.push_back(rec);
        Location to;
        if (!s.location.empty())
            for (const auto& l : locations())
                if (l.id == s.location) to = l;
        // a step's place in the package's own stage: the stage's game name (refused before anything changes)
        if (!to.id.empty() && !resolve_warp_stage(to, host_->mod_stages(), why)) return why = "can't warp to " + to.label + ": " + why, r;
        const bool play = p.get("play").type == Value::Bool ? p.get("play").boolean : true;
        if (!host_->start_at_step(pkg->mod, erase, step_records(pkg->mod, quest, s), s.items, to, play, why)) return r;
        r["requested"] = true;
        r["quest"] = quest;
        r["step"] = int(s.step);
        r["path"] = arr();
        for (const auto& n : s.path) r["path"].array.push_back(Value(n));
        r["flags"] = arr();
        for (const auto& [id, on] : s.flags) {
            Value v;
            v["id"] = id;
            v["value"] = on;
            r["flags"].array.push_back(v);
        }
        r["items"] = arr();
        for (const auto& [id, count] : s.items) {
            Value v;
            v["id"] = id;
            v["count"] = count;
            r["items"].array.push_back(v);
        }
        if (!to.id.empty()) r["warp"] = to.id;
        if (!s.reachable) r["note"] = "nothing declared leads to this step: only the quest's step is set";
        ok = true;
        return r;
    }
    if (command == "quest_state") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        std::vector<Record> keys;
        for (const auto& q : pkg->manifest.get("quests").array) {
            keys.push_back({story_mail::kRecordQuest, quest_key(pkg->mod, q.get("id").string())});
            keys.push_back({story_mail::kRecordTracked, quest_key(pkg->mod, q.get("id").string())});
        }
        std::vector<int> values;
        std::string w;
        const bool live = host_ && !keys.empty() && host_->read_records(keys, values, w) && values.size() == keys.size();
        r["live"] = live;
        r["quests"] = arr();
        size_t i = 0;
        for (const auto& q : pkg->manifest.get("quests").array) {
            const int step = live ? std::max(0, values[i * 2]) : 0;
            const bool tracked = live && values[i * 2 + 1] > 0;
            Value v;
            v["quest"] = q.get("id");
            v["title"] = q.get("title");
            v["step"] = step;
            v["tracked"] = tracked;
            v["text"] = quest_progress_text(q, step);
            r["quests"].array.push_back(v);
            // the status bar's: the tracked quest in progress, else the first in progress
            if (step > 0 && step < 255 && (tracked || r.get("current").type == Value::Null)) r["current"] = v;
            i++;
        }
        ok = true;
        return r;
    }
    if (command == "list_flags") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const auto flags = mod_flags(pkg->mod, pkg->manifest);
        std::vector<Record> keys;
        for (const auto& f : flags) keys.push_back(f.record);
        std::vector<int> values;
        std::string w;
        const bool live = host_ && !keys.empty() && host_->read_records(keys, values, w) && values.size() == keys.size();
        r["live"] = live;
        if (host_) r["test_mode"] = host_->test_mode();
        r["flags"] = arr();
        for (size_t i = 0; i < flags.size(); i++) {
            Value v;
            v["id"] = flags[i].id;
            v["kind"] = flags[i].kind;
            v["settable"] = flags[i].settable;
            v["used_by"] = arr();
            for (const auto& u : flags[i].used_by) v["used_by"].array.push_back(Value(u));
            if (live) v["value"] = values[i] > 0;
            r["flags"].array.push_back(v);
        }
        ok = true;
        return r;
    }
    if (command == "set_flag") {
        Package* pkg = package(p, why);
        if (!pkg || !test_only()) return r;
        const std::string id = text(p, "id");
        for (const auto& f : mod_flags(pkg->mod, pkg->manifest))
            if (f.id == id && f.settable) {
                if (!host_->write_records(pkg->mod, {{f.record, p.get("value").boolean ? 1 : 0}}, why)) return r;
                r["id"] = id;
                r["value"] = p.get("value").boolean;
                r["requested"] = true;
                ok = true;
                return r;
            }
        return why = "\"" + id + "\" is not a flag of " + pkg->mod + " the editor sets (list_flags)", r;
    }
    if (command == "advance_days") {
        if (!test_only()) return r;
        const Value& n = p.get("days");
        if (n.type != Value::Number || !std::isfinite(n.number) || n.number < 1 || n.number > 255 || std::floor(n.number) != n.number)
            return why = "\"days\" is a whole number, 1..255", r;
        if (!host_->advance_days(int(n.number), why)) return r;
        r["days"] = int(n.number);
        r["requested"] = true;
        ok = true;
        return r;
    }
    if (command == "search") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        r = search_package(pkg->mod, pkg->manifest, text(p, "text"));
        ok = true;
        return r;
    }
    if (command == "copy") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        Value clip;
        clip["mod"] = pkg->mod;
        clip["items"] = arr();
        if (p.get("ids").type == Value::Array) {
            clip["kind"] = "actors";
            for (const auto& id : strings(p.get("ids"))) {
                const Value* a = by_id(pkg->manifest.get("actors"), id);
                if (!a) return why = "no placed actor \"" + id + "\"", r;
                clip["items"].array.push_back(*a);
            }
        } else if (p.get("steps").type == Value::Array) {
            clip["kind"] = "quest_steps";
            clip["quest"] = quest;
            const Value* q = by_id(pkg->manifest.get("quests"), quest);
            if (!q) return why = "no quest \"" + quest + "\"", r;
            for (const auto& n : p.get("steps").array) {
                const Value* s = nullptr;
                for (const auto& st : q->get("steps").array)
                    if (st.get("n") == n) s = &st;
                if (!s) return why = "quest " + quest + " has no step " + mods::json::dump(n), r;
                clip["items"].array.push_back(*s);
            }
        } else
            return why = "copy takes actor \"ids\", or a \"quest\" and its \"steps\"", r;
        if (clip["items"].array.empty()) return why = "nothing to copy", r;
        clipboard_ = clip;
        r["kind"] = clip["kind"];
        r["count"] = int(clip["items"].array.size());
        ok = true;
        return r;
    }
    if (command == "paste") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (clipboard_.type != Value::Object) return why = "nothing copied", r;
        const std::string kind = clipboard_.get("kind").string();
        r["ids"] = arr();
        if (kind == "actors") {
            const auto& items = clipboard_.get("items").array;
            const std::string at = p.get("at").string("offset");
            WorldPoint offset{100, 0, 0}, first;
            Place place;
            if (at == "offset") {
                if (p.get("offset").type != Value::Null && !point_of(p.get("offset"), offset)) return why = "\"offset\" is {x, y, z}", r;
            } else if (at == "link" || at == "camera" || at == "position") {
                if (!host_) return why = "the game is not running", r;
                place = at == "camera" ? host_->camera() : host_->link();
                if (!place.valid) return why = at == "camera" ? "the fly camera is off" : "Link is not in the scene", r;
                if (at == "position" && !point_of(p.get("position"), place.position)) return why = "at = position needs \"position\": {x, y, z}", r;
                point_of(items.front().get("position"), first);
                offset = {place.position.x - first.x, place.position.y - first.y, place.position.z - first.z};
            } else
                return why = "\"at\" is offset, link, camera or position", r;
            std::vector<Value> added;
            Value list = pkg->manifest.get("actors").type == Value::Array ? pkg->manifest.get("actors") : arr();
            for (const auto& a : items) {
                const std::string id = free_id(list, a.get("id").string());
                Value copy = duplicate_actor(a, id);
                WorldPoint pos;
                const bool has_position = point_of(copy.get("position"), pos);
                if (place.valid) {  // into the room Link (or the camera) is in
                    copy["stage"] = place.stage;
                    copy["room"] = place.stage == "sea" ? sea_room(pos.x + offset.x, pos.z + offset.z) : place.room;
                }
                // an island ground on the sea stays at the sea's level
                if (has_position) copy["position"] = rounded({pos.x + offset.x, sea_level_piece(copy) ? 0.0f : pos.y + offset.y, pos.z + offset.z});
                list.array.push_back(copy);
                added.push_back(copy);
                r["ids"].array.push_back(Value(id));
            }
            ok = change(*pkg, "Paste " + std::to_string(added.size()) + " actor(s)", [&](Value& m, std::string&) {
                if (m.get("actors").type != Value::Array) m["actors"] = arr();
                for (const auto& a : added) m["actors"].array.push_back(a);
                return true;
            }, r, why);
            return r;
        }
        if (kind == "quest_steps") {
            const std::string into = quest.empty() ? clipboard_.get("quest").string() : quest;
            ok = change(*pkg, "Paste " + std::to_string(clipboard_.get("items").array.size()) + " step(s) into " + into, [&](Value& m, std::string& w) {
                Value* q = by_id(m["quests"], into);
                if (!q) return w = "no quest \"" + into + "\" in " + pkg->mod, false;
                const auto numbers = numbers_of(*q);
                unsigned next = numbers.empty() ? 1 : numbers.back() + 1;
                for (Value s : clipboard_.get("items").array) {
                    if (next > 254) return w = "a quest has steps 1..254", false;
                    s["n"] = int(next);
                    r["ids"].array.push_back(Value(int(next)));
                    (*q)["steps"].array.push_back(s);
                    next++;
                }
                return true;
            }, r, why);
            if (ok) r["quest"] = into;
            return r;
        }
        return why = "nothing to paste", r;
    }
    if (command == "move_actors" || command == "remove_actors") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const auto ids = strings(p.get("ids"));
        if (ids.empty()) return why = "no actors named (\"ids\")", r;
        WorldPoint offset;
        if (command == "move_actors" && !point_of(p.get("offset"), offset)) return why = "\"offset\" is {x, y, z}", r;
        ok = change(*pkg, (command == "move_actors" ? "Move " : "Remove ") + std::to_string(ids.size()) + " actor(s)", [&](Value& m, std::string& w) {
            for (const auto& id : ids)
                if (!by_id(m["actors"], id)) return w = "no placed actor \"" + id + "\" in " + pkg->mod, false;
            auto& list = m["actors"].array;
            if (command == "remove_actors") {
                list.erase(std::remove_if(list.begin(), list.end(), [&](const Value& a) { return std::find(ids.begin(), ids.end(), a.get("id").string()) != ids.end(); }),
                           list.end());
                return true;
            }
            for (auto& a : list) {
                WorldPoint pos;
                if (std::find(ids.begin(), ids.end(), a.get("id").string()) == ids.end() || !point_of(a.get("position"), pos)) continue;
                a["position"] = rounded({pos.x + offset.x, sea_level_piece(a) ? 0.0f : pos.y + offset.y, pos.z + offset.z});
            }
            return true;
        }, r, why);
        if (ok) {
            r["ids"] = arr();
            for (const auto& id : ids) r["ids"].array.push_back(Value(id));
        }
        return r;
    }
    handled = false;
    return r;
}

}  // namespace editor
