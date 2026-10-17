// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct Cpu;
namespace story_prop_textures {
struct Context {uint32_t actor=0,pid=0,epoch=0,generation=0,type=0,private_begin=0,private_end=0;};
struct Replacement {std::string texture;uint32_t definition=0;};
// Must prove current own declaration/config fingerprint, native PID/generation/epoch,
// and exact native solid-heap private allocation range. Never infer ownership from profile.
using VerifyContext=bool(*)(const Context&);
bool configure(VerifyContext);
bool supported(uint32_t type,const std::string& texture);
// Native Create phase4 only, before first Draw; also explicit validated full-state rebind.
// No hook installation, Draw-time swaps, or retail resource mutation.
bool apply(Cpu*,const Context&,uint32_t native_create_result,const std::vector<Replacement>&,bool ready_only=false);
}
