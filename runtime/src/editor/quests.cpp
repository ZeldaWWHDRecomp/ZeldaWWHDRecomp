// World editor: the quest editor's pure helpers (quests.h). No game dependencies.
#include "quests.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <map>
#include <set>

#include "../mods/story_items_core.h"
#include "../mods/story_mail_book.h"
#include "../mods/story_notebook_list.h"

namespace editor {
namespace {

Value arr() {
    Value v;
    v.type = Value::Array;
    return v;
}
const Value* by_id(const Value& list, const std::string& id) {
    for (const auto& v : list.array)
        if (v.get("id").type == Value::String && v.get("id").text == id) return &v;
    return nullptr;
}
Value* by_id(Value& list, const std::string& id) {
    for (auto& v : list.array)
        if (v.get("id").type == Value::String && v.get("id").text == id) return &v;
    return nullptr;
}
bool whole(const Value& v) { return v.type == Value::Number && std::isfinite(v.number) && std::floor(v.number) == v.number; }
unsigned step_n(const Value& s) { return whole(s.get("n")) && s.get("n").number >= 1 && s.get("n").number <= 254 ? unsigned(s.get("n").number) : 0; }
// An actor's dialogues: the actor itself (its when / then) and each of its talks.
template <class F>
void dialogues(Value& a, F f) {
    f(a);
    if (a.get("talks").type == Value::Array)
        for (auto& t : a["talks"].array) f(t);
}
std::string node_of(unsigned n) { return n ? "step:" + std::to_string(n) : std::string("start"); }
Value badge(const std::string& kind, const std::string& text) {
    Value b;
    b["kind"] = kind;
    b["text"] = text;
    return b;
}
// "item:key" -> "key"
std::string item_id(const std::string& ref) { return ref.rfind("item:", 0) == 0 ? ref.substr(5) : std::string(); }

// One way the quest moves on, as the manifest declares it.
struct Edge {
    std::string from, to;       // node ids
    std::string via_kind, via;  // letter, actor, item, scene, talk
    std::string label;
    Value conditions = arr(), rewards = arr();
    bool editable = false;
    // for start-at-step: what taking it hands out
    std::vector<std::pair<std::string, bool>> flags;
    std::vector<std::pair<std::string, int>> items;  // + give, - take
    std::string letter;
    bool carried = false;
    std::string actor;  // the actor to stand in front of
};

std::string actor_verb(const Value& a) {
    const std::string actor = a.get("actor").string();
    if (actor == "sign") return "read ";
    if (actor == "chest") return "open ";
    if (actor == "switch") return "press ";
    return "talk to ";
}

// the rewards and flags of a list of actions (except the quest steps)
void actions_of(const Value& then, const std::string& quest, Edge& e) {
    for (const auto& t : then.array) {
        const std::string action = t.get("action").string(), id = t.get("id").string();
        if (action == "give" || action == "take") {
            const int count = whole(t.get("count")) ? int(t.get("count").number) : 1;
            e.items.push_back({item_id(id), action == "give" ? count : -count});
            Value r;
            r["kind"] = action;
            r["id"] = id;
            r["count"] = count;
            r["text"] = (action == "give" ? "gives " : "takes ") + std::to_string(count) + " " + item_id(id);
            e.rewards.array.push_back(r);
        } else if (action == "flag") {
            const bool on = t.get("value").type == Value::Bool ? t.get("value").boolean : true;
            e.flags.push_back({id, on});
            Value r;
            r["kind"] = "flag";
            r["id"] = id;
            r["value"] = on;
            r["text"] = (on ? "sets " : "clears ") + id;
            e.rewards.array.push_back(r);
        } else if (action == "scene") {
            Value r;
            r["kind"] = "scene";
            r["id"] = id;
            r["text"] = "plays scene " + id;
            e.rewards.array.push_back(r);
        } else if (action == "quest_step" && id != quest) {
            Value r;
            r["kind"] = "quest";
            r["id"] = id;
            r["text"] = "quest " + id + " step " + std::to_string(int(t.get("step").number));
            e.rewards.array.push_back(r);
        }
    }
}

// Every way the quest moves on (declaration order: letter, actors, items, scenes, talks).
std::vector<Edge> edges_of(const Value& m, const Value& q) {
    const std::string quest = q.get("id").string();
    std::vector<Edge> out;
    std::vector<unsigned> steps;
    for (const auto& s : q.get("steps").array)
        if (unsigned n = step_n(s)) steps.push_back(n);
    std::sort(steps.begin(), steps.end());
    const unsigned first = steps.empty() ? 0 : steps.front();
    auto has_step = [&](unsigned n) { return std::find(steps.begin(), steps.end(), n) != steps.end(); };
    // the letter: read, the quest is at its first step
    if (q.get("letter").type == Value::Object && first) {
        Edge e;
        e.from = "start", e.to = node_of(1);
        if (!has_step(1)) e.to = node_of(first);
        e.via_kind = "letter";
        e.via = q.get("letter").get("id").string();
        e.label = "letter from " + q.get("letter").get("sender").string("the sender") + " read";
        e.letter = e.via;
        for (const auto& enc : q.get("letter").get("encloses").array) {
            const std::string id = enc.type == Value::Object ? enc.get("id").string() : enc.string();
            if (id.rfind("letter:", 0) == 0) e.carried = true;
            if (id.rfind("item:", 0) == 0) {
                e.items.push_back({item_id(id), 1});
                e.rewards.array.push_back(badge("give", "encloses " + item_id(id)));
            }
        }
        out.push_back(std::move(e));
    }
    // actors: each dialogue (the actor's own, or each of its talks) whose actions set a step of this
    // quest (quest_step) or finish it (quest_done); its conditions say from which step
    for (const auto& a : m.get("actors").array) {
        std::vector<std::pair<const Value*, int>> dialogues = {{&a, 0}};  // 0: the actor's own, n: talk n
        for (size_t k = 0; k < a.get("talks").array.size(); k++) dialogues.push_back({&a.get("talks").array[k], int(k + 1)});
        for (const auto& [d, talk] : dialogues) {
            // the conditions: the actor's own when, and the talk's besides
            std::vector<const Value*> whens = {&a.get("when")};
            if (talk) whens.push_back(&d->get("when"));
            unsigned at = 0;  // the step of this quest it needs
            for (const Value* w : whens) {
                if (w->get("quest").string() == quest && whole(w->get("step"))) at = unsigned(w->get("step").number);
                for (const auto& qs : w->get("quests").array)
                    if (qs.get("quest").string() == quest && whole(qs.get("step"))) at = unsigned(qs.get("step").number);
            }
            for (const auto& t : d->get("then").array) {
                const std::string action = t.get("action").string();
                if ((action != "quest_step" && action != "quest_done") || t.get("id").string() != quest) continue;
                Edge e;
                e.from = node_of(at);
                e.to = action == "quest_done" ? std::string("end") : node_of(whole(t.get("step")) ? unsigned(t.get("step").number) : 0);
                e.via_kind = "actor";
                e.via = a.get("id").string();
                e.actor = e.via;
                e.label = actor_verb(a) + e.via + (talk ? " (talk " + std::to_string(talk) + ")" : std::string()) + (at ? "" : " (any time)");
                e.editable = !talk && action == "quest_step";  // connect / disconnect change the actor's own dialogue
                for (const Value* w : whens) {
                    auto cond = [&](const std::string& kind, const Value& id, const std::string& text) {
                        Value c;
                        c["kind"] = kind;
                        c["id"] = id;
                        c["text"] = text;
                        e.conditions.array.push_back(c);
                    };
                    if (w->get("quest").type == Value::String && w->get("quest").text != quest)
                        cond("quest", w->get("quest"), "quest " + w->get("quest").text + " at step " + std::to_string(int(w->get("step").number)));
                    for (const auto& qs : w->get("quests").array)
                        if (qs.get("quest").string() != quest)
                            cond("quest", qs.get("quest"), "quest " + qs.get("quest").string() + " at step " + std::to_string(int(qs.get("step").number)));
                    if (w->get("item").type == Value::String) cond("item", w->get("item"), "carries " + item_id(w->get("item").text));
                    if (w->get("time").type == Value::String) cond("time", w->get("time"), "by " + std::string(w->get("time").text == "night" ? "night" : "day"));
                    for (const char* k : {"flag", "not_flag"}) {
                        const Value& f = w->get(k);
                        std::vector<std::string> ids;
                        if (f.type == Value::String) ids.push_back(f.text);
                        for (const auto& x : f.array) ids.push_back(x.string());
                        for (const auto& id : ids) cond("flag", Value(id), std::string(k[0] == 'n' ? "flag " + id + " not set" : "flag " + id + " set"));
                    }
                }
                if (a.get("flag").type == Value::String) e.flags.push_back({a.get("flag").text, true});
                actions_of(d->get("then"), quest, e);
                if (action == "quest_done") e.rewards.array.push_back(badge("done", "finishes the quest"));
                out.push_back(std::move(e));
            }
        }
    }
    // items whose use sets a step: any time Link has one
    for (const auto& it : m.get("items").array) {
        const Value& use = it.get("use");
        if (use.get("action").string() != "quest_step" || use.get("quest").string() != quest) continue;
        Edge e;
        e.from = "start";
        e.to = node_of(whole(use.get("step")) ? unsigned(use.get("step").number) : 0);
        e.via_kind = "item";
        e.via = it.get("id").string();
        e.label = "use " + e.via;
        Value c;
        c["kind"] = "item";
        c["id"] = "item:" + e.via;
        c["text"] = "has " + e.via;
        e.conditions.array.push_back(c);
        e.items.push_back({e.via, 0});  // at least one (below)
        out.push_back(std::move(e));
    }
    // scenes whose actions set a step: from the step that starts them
    for (const auto& sc : m.get("scenes").array) {
        const Value& trig = sc.get("trigger");
        std::string from = "start";
        if (trig.get("type").string() == "quest_step" && trig.get("quest").string() == quest && whole(trig.get("step")))
            from = node_of(unsigned(trig.get("step").number));
        else  // an actor gated at a step of this quest that plays it
            for (const auto& a : m.get("actors").array) {
                std::vector<const Value*> ds = {&a};
                for (const auto& t : a.get("talks").array) ds.push_back(&t);
                for (const Value* d : ds) {
                    const Value& when = d->get("when").get("quest").type == Value::String ? d->get("when") : a.get("when");
                    if (when.get("quest").string() != quest || !whole(when.get("step"))) continue;
                    for (const auto& t : d->get("then").array)
                        if (t.get("action").string() == "scene" && t.get("id").string() == sc.get("id").string())
                            from = node_of(unsigned(when.get("step").number));
                }
            }
        for (const auto& st : sc.get("steps").array)
            for (const auto& t : st.get("then").array) {
                const std::string action = t.get("action").string();
                if ((action != "quest_step" && action != "quest_done") || t.get("id").string() != quest) continue;
                Edge e;
                e.from = from;
                e.to = action == "quest_done" ? std::string("end") : node_of(whole(t.get("step")) ? unsigned(t.get("step").number) : 0);
                e.via_kind = "scene";
                e.via = sc.get("id").string();
                e.label = "scene " + e.via + " ends";
                actions_of(st.get("then"), quest, e);
                out.push_back(std::move(e));
            }
    }
    // a step's resident talk finishes the quest
    for (const auto& s : q.get("steps").array) {
        const Value& npc = s.get("npc");
        if (npc.type != Value::Object || !step_n(s)) continue;
        Edge e;
        e.from = node_of(step_n(s));
        e.to = "end";
        e.via_kind = "talk";
        e.via = npc.get("who").string();
        e.label = "talk to " + e.via;
        if (npc.get("when").get("carries").type == Value::String) {
            Value c;
            c["kind"] = "letter";
            c["id"] = npc.get("when").get("carries");
            c["text"] = "carries the letter";
            e.conditions.array.push_back(c);
        }
        if (npc.get("then").get("give").type == Value::String) e.rewards.array.push_back(badge("give", "gives " + npc.get("then").get("give").text));
        out.push_back(std::move(e));
    }
    return out;
}

const Value* find_quest(const Value& m, const std::string& quest) { return by_id(m.get("quests"), quest); }
std::vector<unsigned> step_numbers(const Value& q) {
    std::vector<unsigned> out;
    for (const auto& s : q.get("steps").array)
        if (unsigned n = step_n(s)) out.push_back(n);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
// nodes reachable from start (BFS), with the edge that first reached each
// `steps_first`: from the start only to the lowest step something starts the quest at, so a way through
// the earlier steps wins over a shortcut (an item usable any time)
std::map<std::string, int> reach(const std::vector<Edge>& edges, bool steps_first = false) {
    unsigned lowest = 0;
    if (steps_first)
        for (const auto& e : edges)
            if (e.from == "start" && e.to.rfind("step:", 0) == 0) {
                const unsigned n = unsigned(std::stoul(e.to.substr(5)));
                if (!lowest || n < lowest) lowest = n;
            }
    std::map<std::string, int> by;  // node -> edge index (-1: start)
    by["start"] = -1;
    std::deque<std::string> todo{"start"};
    while (!todo.empty()) {
        const std::string at = todo.front();
        todo.pop_front();
        for (size_t i = 0; i < edges.size(); i++)
            if (edges[i].from == at && !by.contains(edges[i].to) && (!lowest || at != "start" || edges[i].to == "step:" + std::to_string(lowest))) {
                by[edges[i].to] = int(i);
                todo.push_back(edges[i].to);
            }
    }
    return by;
}
std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
}  // namespace

uint16_t quest_key(const std::string& mod, const std::string& quest) { return story_mail::letter_key(mod, quest); }
uint16_t mod_item_key(const std::string& mod, const std::string& item) { return story_items::item_key(mod, item); }

bool quest_graph(const std::string& mod, const Value& m, const std::string& quest, Value& out) {
    const Value* q = find_quest(m, quest);
    if (!q) return false;
    const auto edges = edges_of(m, *q);
    const auto reached = reach(edges);
    out = Value();
    out["quest"] = quest;
    out["title"] = q->get("title");
    out["nodes"] = arr();
    out["edges"] = arr();
    // problems by node
    std::map<std::string, Value> problems;
    for (const auto& pr : quest_problems(mod, m))
        if (pr.id == quest) {
            const std::string node = pr.field.rfind("step:", 0) == 0 ? pr.field : std::string("start");
            if (problems[node].type != Value::Array) problems[node] = arr();
            problems[node].array.push_back(Value(pr.message));
        }
    Value start;
    start["id"] = "start";
    start["kind"] = "start";
    start["text"] = q->get("start").get("text").string(q->get("letter").type == Value::Object ? "the letter arrives" : "the mod is on");
    start["badges"] = arr();
    if (q->get("letter").type == Value::Object) start["badges"].array.push_back(badge("letter", "letter " + q->get("letter").get("id").string()));
    start["problems"] = problems["start"].type == Value::Array ? problems["start"] : arr();
    out["nodes"].array.push_back(start);
    for (const auto& s : q->get("steps").array) {
        const unsigned n = step_n(s);
        if (!n) continue;
        Value v;
        const std::string id = node_of(n);
        v["id"] = id;
        v["kind"] = "step";
        v["n"] = int(n);
        v["text"] = s.get("text").string();
        if (s.get("where").type == Value::String) v["where"] = s.get("where");
        if (s.get("place").type == Value::Object) v["place"] = s.get("place");
        v["badges"] = arr();
        const Value& av = s.get("available");
        if (av.get("event").type != Value::Null || av.get("not_event").type != Value::Null)
            v["badges"].array.push_back(badge("event", "needs game progress: " + av.get("text").string()));
        if (av.get("time").type == Value::String && av.get("time").text != "any") v["badges"].array.push_back(badge("time", "only by " + av.get("time").text));
        if (s.get("npc").type == Value::Object) {
            v["talk"] = s.get("npc").get("who");
            v["badges"].array.push_back(badge("talk", "talk to " + s.get("npc").get("who").string()));
            if (s.get("npc").get("then").get("give").type == Value::String)
                v["badges"].array.push_back(badge("reward", "gives " + s.get("npc").get("then").get("give").text));
        }
        if (s.get("find").type == Value::String) v["badges"].array.push_back(badge("chart", "find " + s.get("find").text));
        if (s.get("sail_line").type != Value::Null) v["badges"].array.push_back(badge("sail", "the King of Red Lions says a line"));
        bool on = false;  // a way on to another step
        for (const auto& e : edges) on = on || (e.from == id && e.to != "end");
        v["final"] = !on;
        v["reachable"] = reached.contains(id);
        v["problems"] = problems[id].type == Value::Array ? problems[id] : arr();
        out["nodes"].array.push_back(v);
    }
    Value end;
    end["id"] = "end";
    end["kind"] = "end";
    end["text"] = "done";
    end["badges"] = arr();
    if (q->get("reward").get("text").type == Value::String) end["badges"].array.push_back(badge("reward", q->get("reward").get("text").text));
    if (q->get("reward").get("item").type == Value::String) end["badges"].array.push_back(badge("reward", "gives " + q->get("reward").get("item").text));
    end["problems"] = arr();
    out["nodes"].array.push_back(end);
    std::map<std::string, int> outs;
    for (const auto& e : edges) outs[e.from]++;
    for (const auto& e : edges) {
        Value v;
        v["from"] = e.from;
        v["to"] = e.to;
        v["via"]["kind"] = e.via_kind;
        v["via"]["id"] = e.via;
        v["label"] = e.label;
        v["conditions"] = e.conditions;
        v["rewards"] = e.rewards;
        v["editable"] = e.editable;
        v["branch"] = outs[e.from] > 1;
        out["edges"].array.push_back(v);
    }
    return true;
}

bool step_start(const std::string& mod, const Value& m, const std::string& quest, unsigned step, const std::string& warp, StepStart& out,
                std::string& why) {
    const Value* q = find_quest(m, quest);
    if (!q) return why = "no quest \"" + quest + "\"", false;
    const auto numbers = step_numbers(*q);
    if (std::find(numbers.begin(), numbers.end(), step) == numbers.end()) return why = "quest " + quest + " has no step " + std::to_string(step), false;
    out = StepStart();
    out.step = step;
    const auto edges = edges_of(m, *q);
    const std::string target = node_of(step);
    auto reached = reach(edges, true);
    if (!reached.contains(target)) reached = reach(edges);
    std::vector<int> path;
    if (reached.contains(target))
        for (std::string at = target; at != "start";) {
            const int e = reached.at(at);
            path.push_back(e);
            at = edges[size_t(e)].from;
        }
    else
        out.reachable = false;
    std::reverse(path.begin(), path.end());
    out.path.push_back("start");
    std::map<std::string, bool> flags;
    std::vector<std::string> flag_order;
    std::map<std::string, int> items;
    std::vector<std::string> item_order;
    auto count_item = [&](const std::string& id, int delta, bool at_least_one) {
        if (id.empty()) return;
        if (!items.contains(id)) item_order.push_back(id), items[id] = 0;
        items[id] = std::max(0, items[id] + delta);
        if (at_least_one) items[id] = std::max(1, items[id]);
    };
    for (int i : path) {
        const Edge& e = edges[size_t(i)];
        out.path.push_back(e.to);
        if (!e.letter.empty()) out.letter = e.letter;
        if (e.carried) out.carried = true;
        for (const auto& [id, on] : e.flags) {
            if (!flags.contains(id)) flag_order.push_back(id);
            flags[id] = on;
        }
        for (const auto& [id, delta] : e.items) count_item(id, delta, e.via_kind == "item" && id == e.via);
        // an actor that needs an item: Link had one there
        for (const auto& c : e.conditions.array)
            if (c.get("kind").string() == "item") count_item(item_id(c.get("id").string()), 0, true);
    }
    for (const auto& id : flag_order) out.flags.push_back({id, flags[id]});
    for (const auto& id : item_order) out.items.push_back({id, items[id]});
    // where to go: the actor that moves the quest on from this step, or the step's place
    std::string next_actor;
    for (const auto& e : edges)
        if (e.from == target && e.via_kind == "actor" && next_actor.empty()) next_actor = e.actor;
    bool place = false;
    for (const auto& s : q->get("steps").array)
        if (step_n(s) == step) place = s.get("place").get("x").type == Value::Number && s.get("place").get("z").type == Value::Number;
    const std::string w = warp.empty() ? "next" : warp;
    if (w != "next" && w != "place" && w != "none") return why = "\"warp\" is next, place or none", false;
    if (w == "next" && !next_actor.empty()) out.location = "actor:" + mod + ":" + next_actor;
    else if ((w == "next" || w == "place") && place) out.location = "step:" + mod + ":" + quest + ":" + std::to_string(step);
    else if (w == "place") return why = "step " + std::to_string(step) + " has no place on the sea chart", false;
    return true;
}

std::vector<RecordValue> step_records(const std::string& mod, const std::string& quest, const StepStart& s) {
    using namespace story_mail;
    std::vector<RecordValue> out;
    const uint16_t key = quest_key(mod, quest);
    out.push_back({{kRecordQuest, key}, int(s.step)});
    out.push_back({{kRecordQuestOrder, key}, 1});
    out.push_back({{kRecordTracked, key}, 1});
    if (!s.letter.empty()) out.push_back({{kRecordLetter, letter_key(mod, s.letter)}, int(kRead)});
    if (s.carried) out.push_back({{kRecordCarried, key}, 1});
    for (const auto& [id, on] : s.flags) out.push_back({{kRecordActorFlag, letter_key(mod, "actor-flag:" + id)}, on ? 1 : 0});
    return out;
}

std::vector<Problem> quest_problems(const std::string& mod, const Value& m) {
    (void)mod;
    std::vector<Problem> out;
    std::set<std::string> broken;  // props' and enemies' flags
    for (const auto& a : m.get("actors").array)
        for (const char* k : {"broken_flag", "defeated_flag"})
            if (a.get(k).type == Value::String) broken.insert(a.get(k).text);
    for (const auto& q : m.get("quests").array) {
        const std::string quest = q.get("id").string();
        auto add = [&](const std::string& message, unsigned n, const std::string& severity = "warning") {
            Problem p{"quest " + quest + (n ? " step " + std::to_string(n) : std::string()) + ": " + message, "quest", quest, n ? "step:" + std::to_string(n) : std::string()};
            p.severity = severity;
            out.push_back(p);
        };
        std::map<unsigned, int> seen;
        for (const auto& s : q.get("steps").array)
            if (unsigned n = step_n(s)) seen[n]++;
        for (const auto& [n, count] : seen)
            if (count > 1) add("declared " + std::to_string(count) + " times", n, "error");
        const auto edges = edges_of(m, q);
        const auto reached = reach(edges);
        const auto numbers = step_numbers(q);
        bool starts = false, ends = false;
        for (const auto& e : edges) {
            starts = starts || e.from == "start";
            ends = ends || e.to == "end";
        }
        if (!starts && !numbers.empty()) add("nothing starts it (a letter, an actor or an item that sets a step); only the mod's code can", 0);
        for (unsigned n : numbers) {
            const std::string id = node_of(n);
            bool in = false, outgoing = false;
            for (const auto& e : edges) {
                in = in || e.to == id;
                outgoing = outgoing || e.from == id;
            }
            if (!reached.contains(id))
                add(in ? "it can't be reached from the start (the steps before it are unreachable)"
                       : "nothing leads to it (no letter, actor, item or scene sets it); only the mod's code can",
                    n);
            else if (!outgoing && n != numbers.back()) add("nothing moves the quest on from here", n);
            for (const auto& s : q.get("steps").array)
                if (step_n(s) == n && s.get("place").type == Value::Null && s.get("npc").type == Value::Null)
                    out.push_back([&] {
                        Problem p{"quest " + quest + " step " + std::to_string(n) + ": no place on the sea chart (the notebook says so)", "quest", quest,
                                  "step:" + std::to_string(n)};
                        p.severity = "warning";
                        return p;
                    }());
        }
        for (const auto& e : edges) {
            if (e.from.rfind("step:", 0) == 0 && e.to.rfind("step:", 0) == 0) {
                const unsigned a = unsigned(std::stoul(e.from.substr(5))), b = unsigned(std::stoul(e.to.substr(5)));
                if (b <= a) add(e.label + " sets step " + std::to_string(b) + ", which is not after this one: steps only go forward, so it does nothing", a);
            }
            // an item a condition needs that nothing gives
            for (const auto& c : e.conditions.array) {
                if (c.get("kind").string() != "item" || e.via_kind == "item") continue;
                const std::string item = item_id(c.get("id").string());
                bool given = false;
                for (const auto& f : edges)
                    for (const auto& [id, delta] : f.items) given = given || (id == item && delta > 0);
                for (const auto& a : m.get("actors").array) {
                    for (const auto& t : a.get("then").array) given = given || (t.get("action").string() == "give" && item_id(t.get("id").string()) == item);
                    for (const auto& tk : a.get("talks").array)
                        for (const auto& t : tk.get("then").array) given = given || (t.get("action").string() == "give" && item_id(t.get("id").string()) == item);
                    for (const auto& d : a.get("drops").array) given = given || item_id(d.get("item").string()) == item;
                }
                for (const auto& l : m.get("letters").array)
                    for (const auto& enc : l.get("encloses").array) given = given || item_id(enc.string()) == item;
                for (const auto& c2 : m.get("charts").array) given = given || item_id(c2.get("item").string()) == item;
                if (!given) {
                    const unsigned a = e.from.rfind("step:", 0) == 0 ? unsigned(std::stoul(e.from.substr(5))) : 0;
                    add(e.label + " needs " + item + ", but nothing in the package gives it", a);
                }
            }
            for (const auto& [id, on] : e.flags)
                if (broken.contains(id)) {
                    const unsigned a = e.from.rfind("step:", 0) == 0 ? unsigned(std::stoul(e.from.substr(5))) : 0;
                    add(e.label + ": \"" + id + "\" is a prop's or enemy's flag, which a flag action doesn't set", a);
                }
        }
        if (!ends && !numbers.empty())
            out.push_back([&] {
                Problem p{"quest " + quest + ": nothing finishes it (a step's resident talk does; else the mod's code)", "quest", quest, ""};
                p.severity = "info";
                return p;
            }());
    }
    return out;
}

std::vector<ModFlag> mod_flags(const std::string& mod, const Value& m) {
    using namespace story_mail;
    std::vector<ModFlag> out;
    auto add = [&](const std::string& id, const std::string& kind, uint8_t record, const std::string& prefix, const std::string& user, bool settable) {
        if (id.empty()) return;
        for (auto& f : out)
            if (f.id == id && f.record.kind == record) {
                if (std::find(f.used_by.begin(), f.used_by.end(), user) == f.used_by.end()) f.used_by.push_back(user);
                return;
            }
        out.push_back({id, kind, {record, letter_key(mod, prefix + id)}, {user}, settable});
    };
    for (const auto& a : m.get("actors").array) {
        const std::string actor = a.get("id").string();
        add(a.get("flag").string(), "actor", kRecordActorFlag, "actor-flag:", actor, true);
        add(a.get("switch_flag").string(), "switch", kRecordActorFlag, "actor-flag:", actor, true);
        add(a.get("broken_flag").string(), "prop", 18, "prop-broken:", actor, false);
        add(a.get("defeated_flag").string(), "enemy", 19, "enemy-defeated:", actor, false);
        for (const auto& t : a.get("then").array)
            if (t.get("action").string() == "flag") add(t.get("id").string(), "action", kRecordActorFlag, "actor-flag:", actor, true);
        for (const auto& tk : a.get("talks").array)
            for (const auto& t : tk.get("then").array)
                if (t.get("action").string() == "flag") add(t.get("id").string(), "action", kRecordActorFlag, "actor-flag:", actor, true);
    }
    for (const auto& s : m.get("scenes").array)
        for (const auto& st : s.get("steps").array)
            for (const auto& t : st.get("then").array)
                if (t.get("action").string() == "flag") add(t.get("id").string(), "action", kRecordActorFlag, "actor-flag:", "scene " + s.get("id").string(), true);
    return out;
}

std::string quest_progress_text(const Value& q, int saved) {
    const std::string title = q.get("title").string(q.get("id").string());
    if (saved <= 0) return title + ": not started";
    if (saved >= 255) return title + ": done";
    const auto numbers = step_numbers(q);
    const auto it = std::find(numbers.begin(), numbers.end(), unsigned(saved));
    const size_t index = it == numbers.end() ? size_t(saved) : size_t(it - numbers.begin()) + 1;
    return title + ": step " + std::to_string(index) + " of " + std::to_string(numbers.size());
}

Value search_package(const std::string& mod, const Value& m, const std::string& text) {
    Value out = arr();
    const std::string needle = lower(text);
    if (needle.empty()) return out;
    auto hit = [&](const std::string& hay) { return lower(hay).find(needle) != std::string::npos; };
    auto add = [&](const std::string& kind, const std::string& id, const std::string& field, const std::string& found) {
        Value v;
        v["kind"] = kind;
        v["id"] = id;
        v["field"] = field;
        v["text"] = found;
        out.array.push_back(v);
        return &out.array.back();
    };
    auto lines = [](const Value& v) {
        std::string s;
        if (v.type == Value::String) return v.text;
        for (const auto& l : v.array) s += (s.empty() ? "" : " / ") + l.string();
        return s;
    };
    for (const auto& a : m.get("actors").array) {
        const std::string id = a.get("id").string();
        if (hit(id)) add("actor", id, "id", id);
        else if (hit(a.get("actor").string())) add("actor", id, "actor", a.get("actor").string());
        else if (hit(a.get("model").string())) add("actor", id, "model", a.get("model").string());
        else if (hit(lines(a.get("lines")))) add("actor", id, "lines", lines(a.get("lines")));
        else
            for (const auto& tk : a.get("talks").array)
                if (hit(lines(tk.get("lines")))) {
                    add("actor", id, "talks", lines(tk.get("lines")));
                    break;
                }
    }
    for (const auto& q : m.get("quests").array) {
        const std::string id = q.get("id").string();
        if (hit(id) || hit(q.get("title").string())) add("quest", id, "title", q.get("title").string());
        for (const auto& s : q.get("steps").array) {
            const std::string t = s.get("text").string() + " " + s.get("where").string() + " " + lines(s.get("npc").get("lines"));
            if (!hit(t)) continue;
            Value* v = add("quest_step", id, "text", s.get("text").string());
            (*v)["quest"] = id;
            (*v)["step"] = s.get("n");
        }
    }
    for (const auto& sc : m.get("scenes").array) {
        const std::string id = sc.get("id").string();
        std::string say;
        int at = 0, n = 0;
        for (const auto& st : sc.get("steps").array) {
            ++n;
            // a step's text (one line or several), its options' labels, its id
            std::string t = st.get("id").string() + " " + lines(st.get("text"));
            for (const auto& o : st.get("options").array) t += " " + o.get("text").string();
            if (say.empty() && hit(t)) say = lines(st.get("text")).empty() ? st.get("id").string() : lines(st.get("text")), at = n;
        }
        if (hit(id)) add("scene", id, "id", id);
        else if (!say.empty()) {
            Value* v = add("scene", id, "text", say);
            (*v)["step"] = at;
        }
    }
    for (const auto& it : m.get("items").array) {
        const std::string id = it.get("id").string();
        if (hit(id) || hit(it.get("name").string())) add("item", id, "name", it.get("name").string());
    }
    for (const auto& f : mod_flags(mod, m))
        if (hit(f.id)) {
            Value* v = add("flag", f.id, f.kind, f.id);
            (*v)["used_by"] = arr();
            for (const auto& u : f.used_by) (*v)["used_by"].array.push_back(Value(u));
        }
    return out;
}

bool renumber_steps(Value& m, const std::string& quest, const std::vector<std::pair<unsigned, unsigned>>& map, std::string& why) {
    Value* q = by_id(m["quests"], quest);
    if (!q) return why = "no quest \"" + quest + "\"", false;
    auto mapped = [&](unsigned n) {
        for (const auto& [a, b] : map)
            if (a == n) return b;
        return n;
    };
    std::set<unsigned> now;
    for (const auto& s : q->get("steps").array) {
        const unsigned n = mapped(step_n(s));
        if (n < 1 || n > 254) return why = "step numbers are 1..254", false;
        if (!now.insert(n).second) return why = "two steps would get number " + std::to_string(n), false;
    }
    // scenes name steps: never changed here
    for (const auto& sc : m.get("scenes").array) {
        const Value& t = sc.get("trigger");
        auto check = [&](unsigned n) {
            if (mapped(n) != n) why = "scene " + sc.get("id").string() + " names step " + std::to_string(n) + " of " + quest + ": change the scene first";
        };
        if (t.get("type").string() == "quest_step" && t.get("quest").string() == quest && whole(t.get("step"))) check(unsigned(t.get("step").number));
        for (const auto& st : sc.get("steps").array)
            for (const auto& a : st.get("then").array)
                if (a.get("action").string() == "quest_step" && a.get("id").string() == quest && whole(a.get("step"))) check(unsigned(a.get("step").number));
        if (!why.empty()) return false;
    }
    for (auto& s : (*q)["steps"].array)
        if (unsigned n = step_n(s)) s["n"] = int(mapped(n));
    if (m.get("actors").type == Value::Array)
    for (auto& a : m["actors"].array)
        dialogues(a, [&](Value& d) {
            if (d.get("when").get("quest").string() == quest && whole(d.get("when").get("step"))) d["when"]["step"] = int(mapped(unsigned(d.get("when").get("step").number)));
            if (d.get("when").get("quests").type == Value::Array)
                for (auto& qs : d["when"]["quests"].array)
                    if (qs.get("quest").string() == quest && whole(qs.get("step"))) qs["step"] = int(mapped(unsigned(qs.get("step").number)));
            if (d.get("then").type == Value::Array)
                for (auto& t : d["then"].array)
                    if (t.get("action").string() == "quest_step" && t.get("id").string() == quest && whole(t.get("step"))) t["step"] = int(mapped(unsigned(t.get("step").number)));
        });
    if (m.get("items").type == Value::Array)
        for (auto& it : m["items"].array)
            if (it.get("use").get("action").string() == "quest_step" && it.get("use").get("quest").string() == quest && whole(it.get("use").get("step")))
                it["use"]["step"] = int(mapped(unsigned(it.get("use").get("step").number)));
    // the steps in their numbers' order
    std::stable_sort((*q)["steps"].array.begin(), (*q)["steps"].array.end(), [](const Value& a, const Value& b) { return step_n(a) < step_n(b); });
    return true;
}

bool remove_step(Value& m, const std::string& quest, unsigned step, std::vector<std::string>& changed, std::string& why) {
    Value* q = by_id(m["quests"], quest);
    if (!q) return why = "no quest \"" + quest + "\"", false;
    for (const auto& sc : m.get("scenes").array) {
        bool names = false;
        const Value& t = sc.get("trigger");
        names = t.get("type").string() == "quest_step" && t.get("quest").string() == quest && t.get("step").number == step;
        for (const auto& st : sc.get("steps").array)
            for (const auto& a : st.get("then").array)
                names = names || (a.get("action").string() == "quest_step" && a.get("id").string() == quest && a.get("step").number == step);
        if (names) return why = "scene " + sc.get("id").string() + " names step " + std::to_string(step) + ": change the scene first", false;
    }
    for (const auto& it : m.get("items").array)
        if (it.get("use").get("quest").string() == quest && it.get("use").get("step").number == step)
            return why = "item " + it.get("id").string() + " sets step " + std::to_string(step) + " when used: change the item first", false;
    auto& steps = (*q)["steps"].array;
    const auto it = std::find_if(steps.begin(), steps.end(), [&](const Value& s) { return step_n(s) == step; });
    if (it == steps.end()) return why = "quest " + quest + " has no step " + std::to_string(step), false;
    steps.erase(it);
    if (m.get("actors").type == Value::Array)
    for (auto& a : m["actors"].array) {
        bool touched = false;
        dialogues(a, [&](Value& d) {
            if (d.get("when").get("quest").string() == quest && d.get("when").get("step").number == step) {
                d["when"].object.erase("quest");
                d["when"].object.erase("step");
                touched = true;
            }
            if (d.get("when").get("quests").type == Value::Array) {
                auto& qs = d["when"]["quests"].array;
                const size_t before = qs.size();
                qs.erase(std::remove_if(qs.begin(), qs.end(), [&](const Value& x) { return x.get("quest").string() == quest && x.get("step").number == step; }), qs.end());
                if (qs.size() != before) touched = true;
                if (qs.empty()) d["when"].object.erase("quests");
            }
            if (d.get("when").type == Value::Object && d.get("when").object.empty()) d.object.erase("when");
            if (d.get("then").type == Value::Array) {
                auto& then = d["then"].array;
                const size_t before = then.size();
                then.erase(std::remove_if(then.begin(), then.end(),
                                          [&](const Value& t) { return t.get("action").string() == "quest_step" && t.get("id").string() == quest && t.get("step").number == step; }),
                           then.end());
                if (then.size() != before) touched = true;
                if (then.empty()) d.object.erase("then");
            }
        });
        if (touched) changed.push_back(a.get("id").string());
    }
    return true;
}

bool connect_steps(Value& m, const std::string& quest, unsigned from, unsigned to, const std::string& via, const std::string& item, const std::string& time,
                   std::string& why) {
    const Value* q = find_quest(m, quest);
    if (!q) return why = "no quest \"" + quest + "\"", false;
    const auto numbers = step_numbers(*q);
    auto has = [&](unsigned n) { return std::find(numbers.begin(), numbers.end(), n) != numbers.end(); };
    if (from && !has(from)) return why = "quest " + quest + " has no step " + std::to_string(from), false;
    if (!has(to)) return why = "quest " + quest + " has no step " + std::to_string(to) + " (the quest ends with a step's resident talk, not an actor)", false;
    if (from && to <= from) return why = "a quest only goes forward: step " + std::to_string(to) + " is not after step " + std::to_string(from), false;
    Value* a = by_id(m["actors"], via);
    if (!a) return why = "no placed actor \"" + via + "\" (an actor moves the quest on when Link talks to it, reads or opens it)", false;
    const Value& when = a->get("when");
    if (from && when.get("quest").type == Value::String) {
        if (when.get("quest").text != quest)
            return why = "actor " + via + " is there only while quest " + when.get("quest").text + " is at step " + std::to_string(int(when.get("step").number)) +
                         ": choose another actor, or change its condition",
                   false;
        bool moves = false;
        for (const auto& t : a->get("then").array) moves = moves || (t.get("action").string() == "quest_step" && t.get("id").string() == quest);
        if (moves && when.get("step").number != from)
            return why = "actor " + via + " already moves the quest on from step " + std::to_string(int(when.get("step").number)) + " (an actor is there at one step)",
                   false;
    }
    if (from) {
        (*a)["when"]["quest"] = quest;
        (*a)["when"]["step"] = int(from);
    } else if (a->get("when").get("quest").string() == quest) {  // any time: no longer only at one step
        (*a)["when"].object.erase("quest");
        (*a)["when"].object.erase("step");
    }
    if (item == "-") (*a)["when"].object.erase("item");
    else if (!item.empty()) (*a)["when"]["item"] = item.rfind("item:", 0) == 0 ? item : "item:" + item;
    if (time == "-") (*a)["when"].object.erase("time");
    else if (!time.empty()) (*a)["when"]["time"] = time;
    if (a->get("when").type == Value::Object && a->get("when").object.empty()) a->object.erase("when");
    if (a->get("then").type != Value::Array) (*a)["then"] = arr();
    for (const auto& t : a->get("then").array)
        if (t.get("action").string() == "quest_step" && t.get("id").string() == quest) {
            if (t.get("step").number == to) return true;  // the conditions changed only
            return why = "actor " + via + " already sets step " + std::to_string(int(t.get("step").number)) + " (one quest step per actor)", false;
        }
    Value t;
    t["action"] = "quest_step";
    t["id"] = quest;
    t["step"] = int(to);
    (*a)["then"].array.push_back(t);
    return true;
}

bool disconnect_steps(Value& m, const std::string& quest, unsigned from, unsigned to, const std::string& via, std::string& why) {
    Value* a = by_id(m["actors"], via);
    if (!a) return why = "no placed actor \"" + via + "\"", false;
    const Value& when = a->get("when");
    if (from && (when.get("quest").string() != quest || when.get("step").number != from))
        return why = "actor " + via + " doesn't move quest " + quest + " on from step " + std::to_string(from), false;
    if (a->get("then").type != Value::Array) return why = "actor " + via + " sets no quest step", false;
    auto& then = (*a)["then"].array;
    const size_t before = then.size();
    then.erase(std::remove_if(then.begin(), then.end(),
                              [&](const Value& t) { return t.get("action").string() == "quest_step" && t.get("id").string() == quest && t.get("step").number == to; }),
               then.end());
    if (then.size() == before) return why = "actor " + via + " doesn't set step " + std::to_string(to) + " of " + quest, false;
    if (then.empty()) a->object.erase("then");
    return true;
}

}  // namespace editor
