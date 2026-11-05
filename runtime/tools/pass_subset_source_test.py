#!/usr/bin/env python3
"""Source check for the pass subset's masked pipeline variants (gfx/vulkan/draw.cpp, pass_reuse.h).

The user's condition: a masked variant is never recorded into the recipe/pipeline database and is
compiled only in the background, never on the render thread (no stutter). These properties live in
pipeline() and draw(), which need a Vulkan device and the game to run; this test pins them in the
source so a later edit can't quietly route a masked variant through a synchronous compile.
"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
draw = (root / "runtime/src/gfx/vulkan/draw.cpp").read_text()
demand = (root / "runtime/src/gfx/vulkan/pipeline_demand.cpp").read_text()
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def body(text, signature):
    """The brace-matched body of the first function whose definition starts with `signature`."""
    start = text.index(signature)
    open_brace = text.index("{", text.index(")", start))
    depth = 0
    for i in range(open_brace, len(text)):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        if depth == 0:
            return text[open_brace:i + 1]
    raise ValueError(signature)


pipe = body(draw, "Pipeline &pipeline(const uint32_t *r")
# 1. a background miss is declined before anything could compile on the render thread
decline = pipe.find("if (variant.background) {\n    ++g_backgroundRequests;")
check(decline >= 0, "pipeline(): the background request check")
gate = pipe[decline:pipe.find("}\n  }", decline)]
for need in ("g_recipeSink", "recordOnly", "!async_pipeline_enabled()", "!pipeline_background_has_room()",
             "return declined_pipeline();"):
    check(need in gate, f"pipeline(): a background miss is declined on {need}")
for sync in ("build_pipeline_async(", "vkCreateGraphicsPipelines(", "take_warm_pipeline(",
             "remember_pipeline_recipe(", "g_recipeSink->push_back"):
    first = pipe.find(sync)
    check(first < 0 or first > decline, f"pipeline(): {sync} is reached before the background decline")
# 2. the admitted path: the background lane only; a full lane declines, never falls back to a build
branch_at = pipe.find("if (variant.background) {\n      pending->ticket = begin_pipeline_background(")
check(branch_at >= 0, "pipeline(): background requests use begin_pipeline_background")
branch = pipe[branch_at:pipe.find("pending->ticket = begin_pipeline_async(", branch_at)]
check("return declined_pipeline();" in branch and "build_pipeline_async" not in branch and
      "vkCreateGraphicsPipelines" not in branch and "remember_pipeline_recipe" not in branch,
      "pipeline(): a background request the lane can't take is declined, not built")
check(branch.rstrip().endswith("}") and "return remember(pipelines.emplace(key, std::move(p)).first->second);" in branch,
      "pipeline(): the admitted background variant is stored pending")
# 3. never a recipe: not on completion, not filed by the setup's sweeps, not a template miss
check("pending->keepRecipe = !covSlot && !variant.background;" in pipe, "pipeline(): keepRecipe off for background variants")
check("const bool keepRecipe = pending.keepRecipe;" in draw and "if (keepRecipe) remember_pipeline_recipe(std::move(recipe));" in draw,
      "resolve_pending_pipeline(): a background variant's recipe is dropped")
check("if (!covSlot && !variant.background && pipeline_groups::sweeping()" in pipe, "pipeline(): sweeps skip background variants")
check("if (!variant.background)\n      for (const uint32_t group : pipeline_groups::sweeping_all())" in pipe,
      "pipeline(): background variants get no sweep groups")
check("if (variant.background) {\n    // (not a template miss" in pipe, "pipeline(): background variants aren't template misses")
drain = body(draw, "void drain_pending_guest_pipelines()")
check("!value.pendingCompile->background" in drain, "the cache checkpoint's drain waits for background variants")
check("pending->background = variant.background;" in pipe, "pipeline(): background pendings are marked")
# 4. every masked request is a background one
for call in re.findall(r"\{\.clearMask = [^}]*\}", draw):
    check(call.endswith(".background = true}"), f"a masked pipeline request that isn't a background one: {call}")
check(not re.search(r"pipeline\([^;{]*clear_mask\(", draw),
      "a pipeline() call passes a clear mask outside a PipelineVariant")
# 5. draw(): the subset only with the async compiler and a ready own pipeline, never during the
#    GamePad backdrop's capture/replay; the masked variant is only polled, never waited for
mv = body(draw, "Pipeline* masked_variant(")
check("PipelineVariant{.clearMask = clear, .background = true}" in mv, "masked_variant(): a background request")
check(re.findall(r"resolve_pending_pipeline\([^)]*\)", mv) == ["resolve_pending_pipeline(*v, false)"],
      "masked_variant(): polls only")
d = body(draw, "void draw(const uint32_t *r, uint32_t prim")
cond = d[d.index("if (pass_reuse::subset_on()"):d.index("ownColors = colors;", d.index("if (pass_reuse::subset_on()"))]
for need in ("own.pipeline && !own.pendingCompile", "async_pipeline_enabled()", "!gamepad_backdrop_capture_active()",
             "!gamepad_backdrop_replaying()", "!backdropReplay", "!covSlot", "!replay", "!aoPrivateReplay", "stereo::frame.mode == stereo::Off"):
    check(need in cond, f"draw(): the subset needs {need}")
check("clouds_pass_subset_ok(colors, depth)" in d, "draw(): toon clouds / haze pipelines checked for the wider pass")
check("if (!subsetPipeline || !subsetPipeline->pipeline)" in d, "draw(): an unready variant falls back to the own pass")
check("if (subsetAbsent && !reusePass)" in d and "++R.passSubsetKept;" in d,
      "draw(): a pass that ends anyway falls back; kept counted only when reused")
# 6. the lane never counts as ordinary work or against its limit
lane = body(demand, "std::optional<PipelineDemandTicket> begin_pipeline_background(")
check("active" not in lane and "c.queue" not in lane, "begin_pipeline_background(): touches the ordinary queue")
check("c.background.size() >= kBackgroundQueue" in lane and not re.search(r"\.wait(_for)?\(", lane),
      "begin_pipeline_background(): declines a full lane without waiting")

if failures:
    for f in failures:
        print("FAIL:", f)
    sys.exit(1)
print("pass subset source check: ok")
