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
// A capture smaller than the pane's target (gfx::gamepad_backdrop::capture_extent):
// the render area becomes the capture's, and the guest-to-pixel factors shrink
// with it, so viewport and scissor (derived from sx/sy) draw the same pane scaled.
template<class Targets>
void backdrop_scale_targets(Targets& t,uint32_t width,uint32_t height) {
 if(!t.width||!t.height||!width||!height||(width==t.width&&height==t.height))return;
 t.sx*=float(width)/float(t.width);t.sy*=float(height)/float(t.height);
 t.width=width;t.height=height;
}
}
