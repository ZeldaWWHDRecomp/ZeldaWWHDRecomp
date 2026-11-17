// native_draw/draw_pass_begin.h: the render pass a draw begins (draw()'s "!reusePass" step), one function for draw()
// and the J3D described draw that begins its pass itself (j3d_desc_passbegin.h, WWHD_NATIVE_J3D_DESC_PASSBEGIN=1).
// Included by gfx/vulkan/draw.cpp inside namespace gfxvk, before draw(); decomp build, fast mode, WWHD_NATIVE_DRAW=ON
// only. The normal build keeps the same code inline in draw() (its objects stay as they were).
// The steps, in draw()'s order: the open pass ended (end_encoder), each attachment uploaded and moved to its
// attachment layout, its layer view; the textures the pass read last time made ready (prefetch); the GPU pass scope;
// the pass trace; begin_pass; R's open-pass fields.
#pragma once

inline void begin_draw_pass(VkCommandBuffer cmd, const std::array<Surface*, 8>& colors, const std::array<uint32_t, 8>& slices,
                            Surface* depth, uint32_t depthSlice, uint32_t width, uint32_t height) {
  end_encoder();
  std::array<VkRenderingAttachmentInfo, 8> attachments{};
  uint32_t ncolor = 0;
  for (int i = 0; i < 8; i++) {
    attachments[i] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (colors[i]) {
      upload_surface(colors[i]);
      transition_image(colors[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
      attachments[i].imageView = layer_view(colors[i], slices[i]);
      ncolor = i + 1;
    }
  }
  VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  if (depth) {
    upload_surface(depth);
    transition_image(depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    da.imageView = layer_view(depth, depthSlice);
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  }
  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri.renderArea.extent = {width, height};
  ri.layerCount = 1;
  ri.colorAttachmentCount = ncolor;
  ri.pColorAttachments = attachments.data();
  ri.pDepthAttachment = depth ? &da : nullptr;
  ri.pStencilAttachment = depth && depth->fmt.stencil ? &da : nullptr;
  // textures this pass read last time it ended for (sample_transition): ready before it begins
  if (pass_reuse::prefetch_on() && stereo::frame.mode == stereo::Off) prefetch_pass_reads(colors, depth, width, height);
  if(R.gpuPassTimestampsEnabled) gpu_begin_render_scope(colors,depth,slices.data(),depthSlice,width,height);
  trace_pass(colors, ncolor, depth, depthSlice, width, height);
  begin_pass(cmd, ri, [&] {
    PassBegin pass; pass.formats.colorCount = ncolor;
    for (uint32_t i = 0; i < ncolor; ++i) if (colors[i]) {
      pass.formats.colors[i] = colors[i]->fmt.pixel; pass.layouts[i] = colors[i]->layout;
    }
    if (depth) {
      pass.formats.depth = depth->fmt.pixel; pass.formats.stencil = depth->fmt.stencil;
      pass.layouts[8] = depth->layout;
    }
    return pass;
  });
  ++R.renderPassCount;
  pass_state().rendering = true;
  pass_state().tracked = true;
  pass_state().colors = colors;
  std::copy(std::begin(slices), std::end(slices), pass_state().slices.begin());
  pass_state().depth = depth;
  pass_state().depthSlice = depthSlice;
  pass_state().width = width;
  pass_state().height = height;
}
