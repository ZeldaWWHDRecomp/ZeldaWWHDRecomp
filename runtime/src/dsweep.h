#pragma once
// D-sweep (off by default; setup's "Preparing shaders: models" runs it, tools/installer/model_sweep.py): draw every
// model of every object archive once with the game's own model and material code, so the renderer creates (and
// records, with the archive) the pipelines those models need. See dsweep.cpp.
#include <cstdint>
#include <string>
struct Cpu;
namespace dsweep {
// The batch being drawn (1-based), 0 outside a sweep; written by the game thread, read by the renderer's key log.
int tag();
// debug (WWHD_VK_PIPELINE_WHO): the guest call chain and the archive resources / actors the drawing code points to
std::string describe_draw_context();
// WWHD_DSWEEP_EXIT: true once the sweep is done (the display then quits in order, so the caches are saved)
bool exit_requested();
// fopAc_Draw hook, after the actor's own draw: runs the sweep from the player's draw (camera and lists are set up).
void after_actor_draw(Cpu* c, uint32_t actor);
// WWHD_DSWEEP_ONLY=<step>: from that logic step on no actor draws itself (the sweep's models are the only models in
// the frame's draw lists), so the pipelines of a run belong to the sweep, not the scene
bool hide_actor_draw(uint32_t actor);
// VPAD buttons a title sweep (WWHD_DSWEEP=title, setup's "Preparing shaders: models") holds at this script frame: A on
// the boot's notice screen and in the title's idle story intro (back to the title), 0 otherwise
uint32_t pad_buttons(uint64_t frame);
// fpcEx_Handler hook, around the game's execute pass: the actor sweep's actors draw but never run their logic
void before_execute(Cpu* c);
void after_execute(Cpu* c);
}  // namespace dsweep
