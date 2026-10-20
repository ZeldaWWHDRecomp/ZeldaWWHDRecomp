// World editor command layer: letters on their own, treasure charts and items (commands.h; the format is
// story_manifest.h, docs/mod-sdk-v2.md "Letters", "Charts and markers", "Items"). No game dependencies:
// Link's and the fly camera's places come through Host. Every change goes through Session::change, so the
// mod manager's own parsers check it and it is one undo step.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "../mods/story_manifest.h"
#include "commands.h"

namespace editor {
namespace {
namespace fs = std::filesystem;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
fs::path utf8_path(const std::string& s) { return fs::u8path(s); }
#pragma GCC diagnostic pop

std::string text(const Value& p, const char* key) { return p.get(key).string(); }
Value list_value() {
    Value v;
    v.type = Value::Array;
    return v;
}
Value* entry_of(Value& list, const std::string& id) {
    if (list.type != Value::Array) return nullptr;
    for (auto& v : list.array)
        if (v.get("id").type == Value::String && v.get("id").text == id) return &v;
    return nullptr;
}
const Value* entry_of(const Value& list, const std::string& id) { return entry_of(const_cast<Value&>(list), id); }
bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
// an id from a name ("Old map" -> "old-map"); `fallback` when nothing is left
std::string id_from(const std::string& name, const std::string& fallback) {
    std::string out;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    if (out.size() > 24) out.resize(24);
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? fallback : out;
}
std::string unused_id(const Value& list, const std::string& base) {
    if (!entry_of(list, base)) return base;
    for (int n = 2;; n++)
        if (!entry_of(list, base + "-" + std::to_string(n))) return base + "-" + std::to_string(n);
}
double tenth(double v) { return std::round(v * 10) / 10; }
std::string singular(const std::string& section) {
    if (section == "quests_format") return section;
    return !section.empty() && section.back() == 's' ? section.substr(0, section.size() - 1) : section;
}
std::string joined(const std::vector<std::string>& list, size_t most = 8) {
    std::string out;
    for (size_t i = 0; i < list.size() && i < most; i++) out += (i ? "; " : "") + list[i];
    if (list.size() > most) out += "; and " + std::to_string(list.size() - most) + " more";
    return out;
}

// the fields each kind takes (the parsers' own: story_manifest.h)
const std::set<std::string> kLetterFields = {"sender", "text", "encloses", "when", "after_days"};
const std::set<std::string> kChartFields = {"name", "description", "place", "item"};
const std::set<std::string> kItemFields = {"name", "description", "icon", "stack", "use", "held_model", "held_frames", "hold_up"};

// The icon path rules of parse_items, then the file itself in the package.
bool icon_in_package(const std::string& dir, const std::string& icon, std::string& why) {
    if (icon.empty() || icon.size() > story_notebook::kMaxItemIconBytes || icon.find("..") != std::string::npos || icon[0] == '/' ||
        icon.find('\\') != std::string::npos || icon.size() < 5 || icon.compare(icon.size() - 4, 4, ".png") != 0)
        return why = "\"icon\" must name a PNG in the package (a path inside it ending in .png, up to 128 bytes)", false;
    if (dir.empty()) return true;  // no package folder (a test): the path rules only
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(utf8_path(dir), ec), file = fs::weakly_canonical(utf8_path(dir) / utf8_path(icon), ec);
    const auto rel = file.lexically_relative(root);
    if (ec || rel.empty() || rel.begin()->string() == "..") return why = "the icon " + icon + " is outside the package", false;
    unsigned w = 0, h = 0;
    if (!fs::is_regular_file(file, ec)) return why = "the icon " + icon + " is not in the package (icon_file copies a PNG in)", false;
    if (!check_icon_file(file.string(), w, h, why)) return why = "the icon " + icon + ": " + why, false;
    return true;
}

// Copies a PNG into the package's icons folder as icons/<id>.png (icons/<id>-2.png... when a different
// file has that name: an undo still finds the old one). `created`: a new file was written.
bool copy_icon(const std::string& dir, const std::string& file, const std::string& id, std::string& icon, bool& created, std::string& why) {
    created = false;
    if (dir.empty()) return why = "the mod has no package folder", false;
    unsigned w = 0, h = 0;
    if (!check_icon_file(file, w, h, why)) return false;
    std::ifstream in(utf8_path(file), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), {});
    std::error_code ec;
    for (int n = 1; n < 100; n++) {
        icon = "icons/" + id + (n > 1 ? "-" + std::to_string(n) : std::string()) + ".png";
        const fs::path to = utf8_path(dir) / utf8_path(icon);
        if (fs::exists(to, ec)) {
            std::ifstream old(to, std::ios::binary);
            const std::string there((std::istreambuf_iterator<char>(old)), {});
            if (there == bytes) return true;  // the same picture: used as it is
            continue;
        }
        fs::create_directories(to.parent_path(), ec);
        std::ofstream out(to, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), std::streamsize(bytes.size()));
        if (!out.good()) return why = "cannot copy the icon into the package", false;
        created = true;
        return true;
    }
    return why = "the package's icons folder has too many pictures named " + id, false;
}

// the references walk: every string that names `want`, and (charts) the salvage triggers that name `chart`
void walk_refs(const Value& v, const std::string& where, const std::string& want, const std::string& chart, std::vector<std::string>& out) {
    if (v.type == Value::String) {
        if (v.text == want) out.push_back(where);
        return;
    }
    if (v.type == Value::Array) {
        for (size_t i = 0; i < v.array.size(); i++) {
            const Value& e = v.array[i];
            std::string label = std::to_string(i + 1);
            if (e.get("id").type == Value::String) label = e.get("id").text;
            else if (e.get("n").type == Value::Number) label = "step " + std::to_string(int(e.get("n").number));
            walk_refs(e, where + " > " + label, want, chart, out);
        }
        return;
    }
    if (v.type != Value::Object) return;
    if (!chart.empty() && v.get("type").string() == "salvage" && v.get("chart").string() == chart) out.push_back(where + " (salvage)");
    for (const auto& [k, x] : v.object) {
        if (x.type == Value::String && k == "id") {  // "> id" says nothing more
            if (x.text == want) out.push_back(where);
            continue;
        }
        walk_refs(x, where + " > " + k, want, chart, out);
    }
}

// An item's held model must be one of the mod's static models (the game refuses the mod otherwise).
bool held_model_ok(const Value& m, const Value& item, std::string& why) {
    const Value& held = item.get("held_model");
    if (held.type != Value::String) return true;
    const Value* model = entry_of(m.get("models"), held.text);
    if (!model) return why = "item " + item.get("id").string() + ": held_model \"" + held.text + "\" is not one of the mod's models (import_model adds one)", false;
    if (model->get("kind").string("static") != "static") return why = "item " + item.get("id").string() + ": held_model must be a static model", false;
    return true;
}
std::string article(const std::string& kind) { return kind == "item" ? "an " : "a "; }

}  // namespace

std::vector<std::string> story_references(const Value& manifest, const std::string& kind, const std::string& id) {
    std::vector<std::string> out;
    if (kind == "letter" || id.empty()) return out;  // nothing in the format names a letter on its own
    const std::string want = kind + ":" + id, own = kind + "s", chart = kind == "chart" ? id : std::string();
    for (const auto& [section, list] : manifest.object) {
        if (list.type != Value::Array) continue;
        for (size_t i = 0; i < list.array.size(); i++) {
            const Value& e = list.array[i];
            if (section == own && e.get("id").string() == id) continue;  // the entry itself
            const std::string label = e.get("id").type == Value::String ? e.get("id").text : std::to_string(i + 1);
            walk_refs(e, singular(section) + " " + label, want, chart, out);
        }
    }
    return out;
}

bool check_icon_file(const std::string& path, unsigned& width, unsigned& height, std::string& why) {
    std::error_code ec;
    const fs::path file = utf8_path(path);
    if (path.empty() || !fs::is_regular_file(file, ec)) return why = "no file " + path, false;
    const auto size = fs::file_size(file, ec);
    if (ec || size > 16u * 1024 * 1024) return why = "a PNG of up to 16 MiB is needed", false;
    std::ifstream in(file, std::ios::binary);
    unsigned char head[24] = {};
    in.read(reinterpret_cast<char*>(head), sizeof head);
    static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (!in || !std::equal(kSignature, kSignature + 8, head) || std::string(reinterpret_cast<const char*>(head + 12), 4) != "IHDR")
        return why = "the file is not a PNG", false;
    auto be32 = [&](int at) { return (unsigned(head[at]) << 24) | (unsigned(head[at + 1]) << 16) | (unsigned(head[at + 2]) << 8) | unsigned(head[at + 3]); };
    width = be32(16), height = be32(20);
    if (width < 1 || height < 1 || width > 2048 || height > 2048) return why = "the picture must be 1..2048 pixels a side (best 128 x 128)", false;
    return true;
}

Value Session::dispatch_story(const std::string& command, const Value& p, std::string& why, bool& ok, bool& handled) {
    Value r;
    ok = false;
    handled = true;
    // the kind: "letter", "chart" or "item"
    const std::string verb = command.substr(0, command.find('_')), noun = command.substr(command.find('_') + 1);
    std::string kind = noun;
    if (verb == "list" && !kind.empty() && kind.back() == 's') kind.pop_back();
    if ((kind != "letter" && kind != "chart" && kind != "item") || (verb != "list" && verb != "add" && verb != "set" && verb != "remove")) {
        handled = false;
        return r;
    }
    const std::string section = kind + "s";
    Package* pkg = package(p, why);
    if (!pkg) return r;
    const std::set<std::string>& allowed = kind == "letter" ? kLetterFields : kind == "chart" ? kChartFields : kItemFields;

    if (verb == "list") {
        r.type = Value::Array;
        for (const auto& e : pkg->manifest.get(section).array) {
            Value v = e;
            v["used_by"] = list_value();
            for (const auto& u : story_references(pkg->manifest, kind, e.get("id").string())) v["used_by"].array.push_back(Value(u));
            if (kind == "chart" && e.get("place").get("x").type == Value::Number && e.get("place").get("z").type == Value::Number) {
                const int square = sea_room(float(e.get("place").get("x").number), float(e.get("place").get("z").number));
                v["square"] = square;
                if (const char* name = island_name(square)) v["island"] = std::string(name);
            }
            if (kind == "item") {
                std::string w;
                v["icon_ok"] = icon_in_package(pkg->dir, e.get("icon").string(), w);
                if (!w.empty()) v["icon_problem"] = w;
            }
            r.array.push_back(v);
        }
        ok = true;
        return r;
    }

    const std::string id_in = text(p, "id");
    if (verb == "remove") {
        if (!entry_of(pkg->manifest.get(section), id_in)) return why = "no " + kind + " \"" + id_in + "\" in " + pkg->mod, r;
        const auto refs = story_references(pkg->manifest, kind, id_in);
        if (!refs.empty())
            return why = "the " + kind + " \"" + id_in + "\" is still used: " + joined(refs) + ". Change or remove those first.", r;
        ok = change(*pkg, "Remove " + kind + " " + id_in, [&](Value& m, std::string&) {
            auto& list = m[section].array;
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Value& e) { return e.get("id").string() == id_in; }), list.end());
            if (list.empty()) m.object.erase(section);
            return true;
        }, r, why);
        if (ok) r["id"] = id_in;
        return r;
    }

    // add and set: the entry's new fields
    Value fields;
    fields.type = Value::Object;
    std::string id = id_in;
    if (verb == "add") {
        for (const auto& k : allowed)
            if (p.get(k).type != Value::Null) fields[k] = p.get(k);
        if (id.empty()) {
            const std::string from = kind == "letter" ? std::string() : text(p, "name");
            id = unused_id(pkg->manifest.get(section), id_from(from, kind));
        }
        if (!valid_id(id)) return why = article(kind) + kind + " id is 1 to 32 of a-z 0-9 - _ .", r;
        if (entry_of(pkg->manifest.get(section), id)) return why = "the mod has a " + kind + " \"" + id + "\" already", r;
        if (kind == "letter")
            for (const auto& q : pkg->manifest.get("quests").array)
                if (q.get("letter").get("id").string() == id) return why = "quest " + q.get("id").string() + "'s letter has the id \"" + id + "\" already", r;
    } else {
        if (!entry_of(pkg->manifest.get(section), id)) return why = "no " + kind + " \"" + id + "\" in " + pkg->mod, r;
        for (const auto& [k, v] : p.get("fields").object)
            if (!allowed.contains(k)) {
                std::string names;
                for (const auto& a : allowed) names += (names.empty() ? "" : ", ") + a;
                return why = "\"" + k + "\" is not a field of " + article(kind) + kind + " (" + names + ")", r;
            }
        fields = p.get("fields");
        if (fields.type != Value::Object) fields.type = Value::Object;
    }
    // a chart's X from where Link or the fly camera is at sea
    if (kind == "chart" && p.get("at").type == Value::String) {
        const std::string at = p.get("at").text;
        if (at != "link" && at != "camera") return why = "\"at\" is link or camera", r;
        if (!host_) return why = "the game is not running", r;
        const Place there = at == "link" ? host_->link() : host_->camera();
        if (!there.valid) return why = at == "link" ? "Link is not in the scene" : "the fly camera is off", r;
        if (there.stage != "sea") return why = "a chart's X is on the Great Sea: " + there.stage + " is not at sea", r;
        Value place;
        place["x"] = tenth(there.position.x);
        place["z"] = tenth(there.position.z);
        fields["place"] = place;
    }
    if (verb == "add" && kind == "chart" && fields.get("place").type == Value::Null) return why = "a chart needs its place: \"place\" {x, z} or \"at\": link or camera", r;
    if (verb == "add" && kind == "letter" && fields.get("encloses").type == Value::Array && fields.get("encloses").array.empty()) fields.object.erase("encloses");
    // an item's icon: copied in from a file, or a PNG the package has
    bool created = false;
    std::string copied;
    if (kind == "item") {
        if (p.get("icon_file").type == Value::String) {
            if (fields.get("icon").type != Value::Null) return why = "give \"icon\" or \"icon_file\", not both", r;
            if (!copy_icon(pkg->dir, p.get("icon_file").text, id, copied, created, why)) return r;
            fields["icon"] = copied;
        } else if (fields.get("icon").type == Value::String) {
            if (!icon_in_package(pkg->dir, fields.get("icon").text, why)) return r;
        } else if (verb == "add")
            return why = "an item needs an icon: \"icon\" (a PNG in the package) or \"icon_file\" (a PNG to copy in)", r;
        if (verb == "set" && fields.object.contains("icon") && fields.get("icon").type == Value::Null) return why = "an item keeps its icon", r;
    }
    for (const char* needed : {"name", "sender", "text", "description"})
        if (verb == "set" && fields.object.contains(needed) && fields.get(needed).type == Value::Null &&
            !(kind == "chart" && std::string(needed) == "description"))
            return why = article(kind) + kind + " keeps its " + needed, r;
    if (verb == "set" && kind == "chart" && fields.object.contains("place") && fields.get("place").type == Value::Null) return why = "a chart keeps its place", r;
    if (verb == "set" && fields.object.empty()) return why = "nothing to set (\"fields\"" + std::string(kind == "chart" ? " or \"at\")" : ")"), r;

    const std::string label = (verb == "add" ? "Add " : "Set ") + kind + " " + id;
    ok = change(*pkg, label, [&](Value& m, std::string& w) {
        if (verb == "add") {
            if (m.get(section).type != Value::Array) m[section] = list_value();
            Value e;
            e["id"] = id;
            for (const auto& [k, v] : fields.object)
                if (v.type != Value::Null) e[k] = v;
            m[section].array.push_back(e);
            return held_model_ok(m, e, w);
        }
        Value* e = entry_of(m[section], id);
        if (!e) return w = "no " + kind + " \"" + id + "\"", false;
        for (const auto& [k, v] : fields.object) {
            if (v.type == Value::Null) e->object.erase(k);
            else (*e)[k] = v;
        }
        return held_model_ok(m, *e, w);
    }, r, why);
    if (!ok) {
        std::error_code ec;
        if (created) fs::remove(utf8_path(pkg->dir) / utf8_path(copied), ec);  // the refused change's own picture
        return r;
    }
    r["id"] = id;
    if (const Value* e = entry_of(pkg->manifest.get(section), id)) r[kind] = *e;
    if (!copied.empty()) r["icon"] = copied;
    return r;
}

}  // namespace editor
