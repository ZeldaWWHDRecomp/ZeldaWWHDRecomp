#pragma once
#include <cstdint>
namespace gfxvk {
// Resources are immutable descriptor/upload bindings from the original draw,
// not guest addresses. Alpha-only pipeline variants retain the same shader
// reflection and dynamic-uniform mode, hence compatible descriptor layouts.
template<class Snapshot,class PipelineType,class Shader,class Resources>
bool backdrop_reuse_resources(const Snapshot& original,const PipelineType& pipeline,
                             Shader* vs,Shader* ps,uint64_t submission,
                             Resources& vertex,Resources& pixel) {
 if(!original.replay||!original.rasterReady||!original.pipeline||!pipeline.layout||
    original.submission!=submission||original.vs!=vs||original.ps!=ps||
    pipeline.dynamicUniforms!=original.pipeline->dynamicUniforms)return false;
 const auto& v=original.stages[0].resources;
 const auto& p=original.stages[1].resources;
 if(!v.set||!p.set||v.dynamicOffsetCount>v.dynamicOffsets.size()||
    p.dynamicOffsetCount>p.dynamicOffsets.size())return false;
 vertex=v;pixel=p;return true;
}
}
