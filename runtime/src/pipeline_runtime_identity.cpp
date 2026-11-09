#include "pipeline_runtime_identity.h"
#ifndef WWHD_PIPELINE_RUNTIME_ID
#define WWHD_PIPELINE_RUNTIME_ID "unknown"
#endif
// The marker lets setup read the identity from the binary without starting it (tools/installer/shader_prebuild.py
// pipeline_identity_of; Android's game is a library setup cannot start).
static const char kMarker[] = "WWHD-PIPELINE-IDENTITY:" WWHD_PIPELINE_RUNTIME_ID;
const char* wwhd::pipeline_runtime_identity() { return kMarker + sizeof("WWHD-PIPELINE-IDENTITY:") - 1; }
