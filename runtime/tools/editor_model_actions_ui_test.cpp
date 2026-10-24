// An imported model's actions in the Outliner (overlay/editor_model_actions.h, issue #287) with the real Dear ImGui code
// and no game: Replace from file... opens the file chooser and replaces the model with the picked file; Watch file is
// off by default, waits for a known file and turns the watch on and off; Remove asks first, lists the placements
// and removes them too only from "Remove those too", removes an unused model at once, can't remove one an item holds,
// and Cancel runs nothing.
#include "../src/overlay/editor_model_actions.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

namespace {
using Value = mods::json::Value;
namespace ma = editor_model_actions;

struct Call {
    std::string name;
    Value params;
};
std::vector<Call> calls;
std::function<void(std::string)> chooser;  // the open file chooser's answer
ma::State state;
Value model, files;

void frame() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoDecoration);
    ma::draw(state, model, files, [](const std::string& n, const Value& p, std::function<void(const Value&)>) { calls.push_back({n, p}); },
             [](std::function<void(std::string)> picked) { chooser = std::move(picked); });
    ImGui::End();
    ImGui::Render();
}
ImVec2 find(ImGuiWindow* window, ImGuiID id) {
    ImGuiIO& io = ImGui::GetIO();
    const ImRect r = window->Rect();
    for (float y = r.Min.y + 2; y < std::min(r.Max.y, r.Min.y + 300); y += 3)
        for (float x = r.Min.x + 6; x < std::min(r.Max.x, r.Min.x + 700); x += 25) {
            io.AddMousePosEvent(x, y);
            frame();
            if (ImGui::GetCurrentContext()->HoveredId == id) return ImVec2(x, y);
        }
    return ImVec2(-1, -1);
}
ImGuiWindow* outliner() { return ImGui::FindWindowByName("Outliner"); }
ImGuiID row(const char* label) { return ImGui::GetIDWithSeed(label, nullptr, outliner()->GetID("ground")); }
ImGuiWindow* modal() {
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->Active && (w->Flags & ImGuiWindowFlags_Modal)) return w;
    return nullptr;
}
void click(ImVec2 at) {
    assert(at.x >= 0 && "the item is on screen");
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
}
Value json(const char* t) { return mods::json::parse(t); }
}  // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    model = json(R"({"id": "ground", "source": "models/ground.glb", "kind": "static"})");

    // not known where it came from (imported in an earlier session): Watch file can't be turned on
    files = json(R"({"placements": ["g1", "g2"], "items": []})");
    for (int i = 0; i < 2; i++) frame();
    click(find(outliner(), row("Watch file")));
    assert(calls.empty() && "Watch file waits for a known file");

    // Replace from file...: the chooser, then replace_model with the picked file
    click(find(outliner(), row("Replace from file...")));
    assert(chooser && calls.empty());
    chooser("/Users/me/blender/ground-v2.glb");
    assert(calls.size() == 1 && calls[0].name == "replace_model" && calls[0].params.get("model").string() == "ground" &&
           calls[0].params.get("file").string() == "/Users/me/blender/ground-v2.glb");
    calls.clear();

    // Watch file, now that the file is known: on, then off
    files["file"] = "/Users/me/blender/ground-v2.glb";
    frame();
    click(find(outliner(), row("Watch file")));
    assert(calls.size() == 1 && calls[0].name == "watch_model" && calls[0].params.get("on").boolean &&
           calls[0].params.get("file").string() == "/Users/me/blender/ground-v2.glb");
    files["watching"] = true;
    frame();
    click(find(outliner(), row("Watch file")));
    assert(calls.size() == 2 && calls[1].name == "watch_model" && !calls[1].params.get("on").boolean && calls[1].params.get("file").type == Value::Null);
    calls.clear();

    // Remove with placements: asked first; Cancel runs nothing
    click(find(outliner(), row("Remove")));
    ImGuiWindow* ask = modal();
    assert(ask && state.removing == "ground");
    assert(find(ask, ask->GetID("Remove")).x < 0 && "no plain Remove while placements use it");
    click(find(ask, ask->GetID("Cancel")));
    assert(!modal() && calls.empty() && state.removing.empty());
    // "Remove those too": the model and its placements, confirmed
    click(find(outliner(), row("Remove")));
    ask = modal();
    click(find(ask, ask->GetID("Remove those too")));
    assert(!modal() && calls.size() == 1 && calls[0].name == "remove_model" && calls[0].params.get("confirm").boolean &&
           calls[0].params.get("actors").boolean && calls[0].params.get("model").string() == "ground");
    calls.clear();

    // unused: Remove removes the model alone
    files["placements"] = json("[]");
    frame();
    click(find(outliner(), row("Remove")));
    ask = modal();
    click(find(ask, ask->GetID("Remove")));
    assert(calls.size() == 1 && calls[0].name == "remove_model" && calls[0].params.get("actors").type == Value::Null);
    calls.clear();

    // held by an item: only Cancel
    files["items"] = json(R"(["lantern"])");
    frame();
    click(find(outliner(), row("Remove")));
    ask = modal();
    assert(ask && find(ask, ask->GetID("Remove")).x < 0 && find(ask, ask->GetID("Remove those too")).x < 0);
    click(find(ask, ask->GetID("Cancel")));
    assert(calls.empty());

    ImGui::DestroyContext();
    std::puts("editor_model_actions_ui: ok");
    return 0;
}
