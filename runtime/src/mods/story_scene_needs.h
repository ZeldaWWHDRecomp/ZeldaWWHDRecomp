// Which manifests need the story system (its required package, the Story Notebook).
//
// Letters, charts, items, quests, models and placed actors are story content: they run only with the
// Story Notebook. Scenes need it only for what the story system brings: text in the message window
// (`say`, `choice`, a demo step's `messages`), UI sounds, music hooks, talk/quest/salvage triggers and
// completion actions other than going into a stage or starting another scene. A package of stages with
// a wordless scene (a cutscene that flies Link to the package's stage, say) stands on its own.
#pragma once
#include "mod_json.h"
#include <set>
#include <string>

namespace story_scene_needs {

inline bool actions_need_story(const mods::json::Value& then) {
    if (then.type != mods::json::Value::Array) return false;
    for (const auto& a : then.array) {
        const std::string kind = a.get("action").string();
        if (kind != "stage" && kind != "scene") return true;
    }
    return false;
}

inline bool scene_needs_story(const mods::json::Value& scene) {
    using mods::json::Value;
    const std::string trigger = scene.get("trigger").get("type").string();
    if (trigger == "talk" || trigger == "quest_step" || trigger == "salvage") return true;
    if (scene.get("native").type != Value::Null) return true;
    const auto& steps = scene.get("steps");
    if (steps.type != Value::Array) return false;
    for (const auto& step : steps.array) {
        const std::string type = step.get("type").string();
        if (type == "say" || type == "choice" || type == "sound" || type == "music") return true;
        if (actions_need_story(step.get("then"))) return true;
        if (type == "demo") {
            const auto& messages = step.get("messages");
            if (messages.type == Value::Array && !messages.array.empty()) return true;
            const auto& actors = step.get("actors");
            if (actors.type == Value::Array)
                for (const auto& a : actors.array)
                    if (a.get("character").type != Value::Null) return true;
        }
    }
    return false;
}

// The manifest has story content that needs the Story Notebook.
inline bool needs_story(const mods::json::Value& manifest) {
    using mods::json::Value;
    for (const char* key : {"quests", "charts", "letters", "items", "models", "actors"})
        if (manifest.get(key).type != Value::Null) return true;
    const auto& scenes = manifest.get("scenes");
    if (scenes.type == Value::Array)
        for (const auto& s : scenes.array)
            if (scene_needs_story(s)) return true;
    return false;
}

// {"id": "story-notebook", "optional": true} in "dependencies": the package plays without the notebook,
// its story content (quests, letters, items, story actions) only with it.
inline bool story_optional(const mods::json::Value& manifest) {
    const auto& deps = manifest.get("dependencies");
    if (deps.type != mods::json::Value::Array) return false;
    for (const auto& d : deps.array)
        if (d.get("id").string() == "story-notebook" && d.get("optional").type == mods::json::Value::Bool && d.get("optional").boolean) return true;
    return false;
}
// The packages whose optional story content is off this session (the Story Notebook isn't there): their story
// actions (quest steps, quest done, give, take, flags) are skipped; stage and scene actions still run.
inline std::set<std::string>& story_off() {
    static std::set<std::string> off;
    return off;
}

}  // namespace story_scene_needs
