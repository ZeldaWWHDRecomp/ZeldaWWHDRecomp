#pragma once
// The identity of the code that interprets saved pipeline recipes and prepared shaders
// (cmake/PipelineRuntimeIdentity.cmake, cmake/pipeline_identity_inputs.txt): equal in two builds when
// their renderers would turn the same recipe into the same pipeline. The Vulkan recipe fingerprint, the
// setup's shader preparation (its "runtime" identity) and the launch check of a prepared generation use it,
// so an update that leaves the renderer as it was keeps the player's prepared shaders.
namespace wwhd { const char* pipeline_runtime_identity(); }
