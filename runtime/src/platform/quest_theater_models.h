// Runtime-supplied glTF controller models.
// Controller vertices retain the asset's grip-relative coordinates and nodes.
struct TheaterMeshVertex {
 XrVector3f position{},normal{0,1,0};XrVector2f uv{};
};
struct TheaterMesh {
 struct Part {GLuint vao=0,vertices=0,indices=0,texture=0;GLsizei count=0;float color[4]{1,1,1,1};};
 std::vector<Part> parts;
 void destroy(){for(auto& p:parts){glDeleteVertexArrays(1,&p.vao);glDeleteBuffers(1,&p.vertices);glDeleteBuffers(1,&p.indices);if(p.texture)glDeleteTextures(1,&p.texture);}parts.clear();}
 void upload(const std::vector<TheaterMeshVertex>& vertices,const std::vector<uint32_t>& indices) {
  if(vertices.empty()||indices.empty())throw std::runtime_error("Empty runtime mesh");
  parts.emplace_back();auto& p=parts.back();p.count=GLsizei(indices.size());
  glGenVertexArrays(1,&p.vao);glBindVertexArray(p.vao);glGenBuffers(1,&p.vertices);glBindBuffer(GL_ARRAY_BUFFER,p.vertices);
  glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(TheaterMeshVertex),vertices.data(),GL_STATIC_DRAW);
  glGenBuffers(1,&p.indices);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,p.indices);glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(uint32_t),indices.data(),GL_STATIC_DRAW);
  auto attribute=[](int slot,int count,size_t offset){glEnableVertexAttribArray(slot);glVertexAttribPointer(slot,count,GL_FLOAT,GL_FALSE,sizeof(TheaterMeshVertex),reinterpret_cast<void*>(offset));};
  attribute(0,3,offsetof(TheaterMeshVertex,position));attribute(1,3,offsetof(TheaterMeshVertex,normal));attribute(2,2,offsetof(TheaterMeshVertex,uv));
 }
 void load_glb(const std::vector<uint8_t>& bytes) {
  cgltf_options options{};cgltf_data* raw=nullptr;
  if(cgltf_parse(&options,bytes.data(),bytes.size(),&raw)!=cgltf_result_success)throw std::runtime_error("Invalid runtime glTF model");
  std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
  // Runtime models are self-contained GLBs. Never follow an asset URI to disk.
  if(data->file_type!=cgltf_file_type_glb)throw std::runtime_error("Runtime model is not GLB");
  for(size_t b=0;b<data->buffers_count;++b)if(data->buffers[b].uri)throw std::runtime_error("External runtime model buffer");
  if(cgltf_load_buffers(&options,data.get(),nullptr)!=cgltf_result_success||cgltf_validate(data.get())!=cgltf_result_success)
   throw std::runtime_error("Invalid runtime model buffers");
  for(size_t n=0;n<data->nodes_count;++n) {
   auto& node=data->nodes[n];if(!node.mesh)continue;
   float transform[16];cgltf_node_transform_world(&node,transform);
   // Bake the runtime asset's default skeleton pose once. Skinned glTF
   // vertices use jointWorld * inverseBind, without the mesh node transform.
   std::vector<std::array<float,16>> jointMatrices;
   if(node.skin) {
    if(!node.skin->joints_count||node.skin->joints_count>1024)throw std::runtime_error("Invalid controller skeleton");
    jointMatrices.resize(node.skin->joints_count);
    for(size_t j=0;j<jointMatrices.size();++j) {
     float world[16],bind[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
     cgltf_node_transform_world(node.skin->joints[j],world);
     if(node.skin->inverse_bind_matrices&&!cgltf_accessor_read_float(node.skin->inverse_bind_matrices,j,bind,16))
      throw std::runtime_error("Unreadable controller bind matrix");
     for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)
      jointMatrices[j][c*4+r]+=world[k*4+r]*bind[c*4+k];
    }
   }
   for(size_t m=0;m<node.mesh->primitives_count;++m) {
    auto& primitive=node.mesh->primitives[m];if(primitive.type!=cgltf_primitive_type_triangles)continue;
    cgltf_accessor *positions=nullptr,*normals=nullptr,*uvs=nullptr,*joints=nullptr,*weights=nullptr;
    for(size_t a=0;a<primitive.attributes_count;++a){auto& attribute=primitive.attributes[a];
     if(attribute.type==cgltf_attribute_type_position)positions=attribute.data;
     if(attribute.type==cgltf_attribute_type_normal)normals=attribute.data;
     if(attribute.type==cgltf_attribute_type_texcoord&&attribute.index==0)uvs=attribute.data;
     if(attribute.type==cgltf_attribute_type_joints&&attribute.index==0)joints=attribute.data;
     if(attribute.type==cgltf_attribute_type_weights&&attribute.index==0)weights=attribute.data;
    }
    if(!positions||positions->count>500000)throw std::runtime_error("Invalid controller vertices");
    if(node.skin&&(!joints||!weights))throw std::runtime_error("Missing controller skin attributes");
    std::vector<TheaterMeshVertex> vertices(positions->count);
    for(size_t v=0;v<vertices.size();++v) {
     float p[3],normal[3]{0,1,0},uv[2]{};
     if(!cgltf_accessor_read_float(positions,v,p,3)||(normals&&!cgltf_accessor_read_float(normals,v,normal,3))||
        (uvs&&!cgltf_accessor_read_float(uvs,v,uv,2)))throw std::runtime_error("Unreadable controller vertex");
     float skin[16]{};const float* matrix=transform;
     if(node.skin) {
      cgltf_uint joint[4]{};float weight[4]{};
      if(!cgltf_accessor_read_uint(joints,v,joint,4)||!cgltf_accessor_read_float(weights,v,weight,4))
       throw std::runtime_error("Unreadable controller skin weights");
      float total=0;
      for(int j=0;j<4;++j) {
       if(!std::isfinite(weight[j])||weight[j]<0)throw std::runtime_error("Invalid controller skin weight");
       if(weight[j]==0)continue;
       if(joint[j]>=jointMatrices.size())throw std::runtime_error("Controller joint out of bounds");
       total+=weight[j];for(int k=0;k<16;++k)skin[k]+=weight[j]*jointMatrices[joint[j]][k];
      }
      if(total<1e-8f)throw std::runtime_error("Empty controller skin weights");
      for(auto& value:skin)value/=total;
      matrix=skin;
     }
     XrVector3f x{matrix[0],matrix[1],matrix[2]},y{matrix[4],matrix[5],matrix[6]},z{matrix[8],matrix[9],matrix[10]};
     auto nx=cross(y,z),ny=cross(z,x),nz=cross(x,y);float det=x.x*nx.x+x.y*nx.y+x.z*nx.z;
     if(std::abs(det)<1e-8f)throw std::runtime_error("Singular controller model node");
     vertices[v].position=add(add(add(mul(x,p[0]),mul(y,p[1])),mul(z,p[2])),{matrix[12],matrix[13],matrix[14]});
     vertices[v].normal=mul(add(add(mul(nx,normal[0]),mul(ny,normal[1])),mul(nz,normal[2])),1.f/det);
     vertices[v].uv={uv[0],uv[1]};
    }
    size_t count=primitive.indices?primitive.indices->count:vertices.size();if(count>1500000||count%3)throw std::runtime_error("Invalid controller triangles");
    std::vector<uint32_t> indices(count);
    for(size_t i=0;i<count;++i){auto index=primitive.indices?cgltf_accessor_read_index(primitive.indices,i):i;
     if(index>=vertices.size())throw std::runtime_error("Controller index out of bounds");indices[i]=uint32_t(index);}
    upload(vertices,indices);auto& part=parts.back();
    if(auto* material=primitive.material) {
     const auto& pbr=material->pbr_metallic_roughness;std::copy_n(pbr.base_color_factor,4,part.color);
     if(auto* texture=pbr.base_color_texture.texture) {
      auto* image=texture->image;
      if(!image||!image->buffer_view)throw std::runtime_error("Runtime controller texture is not embedded");
      auto* view=image->buffer_view;const auto* source=cgltf_buffer_view_data(view);
      if(!source||view->size>16*1024*1024)throw std::runtime_error("Invalid controller texture buffer");
      int w=0,h=0,components=0;auto* pixels=stbi_load_from_memory(source,int(view->size),&w,&h,&components,4);
      if(!pixels)throw std::runtime_error("Runtime controller texture decode failed");
      glGenTextures(1,&part.texture);glBindTexture(GL_TEXTURE_2D,part.texture);
      glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);stbi_image_free(pixels);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);glGenerateMipmap(GL_TEXTURE_2D);
     }
    }
   }
  }
  if(parts.empty())throw std::runtime_error("Runtime controller contains no triangle mesh");
 }
};
struct TheaterRuntimeModels {
 TheaterMesh models[2];XrRenderModelKeyFB loaded[2]{},failed[2]{};XrPath paths[2]{};
 XrResult lastResult[2]{XR_RESULT_MAX_ENUM,XR_RESULT_MAX_ENUM};
 PFN_xrGetRenderModelPropertiesFB properties=nullptr;PFN_xrLoadRenderModelFB loadModel=nullptr;
 XrSession session=XR_NULL_HANDLE;XrTime nextCheck=0;
 void initialize(XrInstance instance,XrSession owner,PFN_xrGetInstanceProcAddr getProc) {
  session=owner;PFN_xrStringToPath stringToPath=nullptr;
  check(getProc(instance,"xrGetRenderModelPropertiesFB",reinterpret_cast<PFN_xrVoidFunction*>(&properties)),"controller model properties function");
  check(getProc(instance,"xrLoadRenderModelFB",reinterpret_cast<PFN_xrVoidFunction*>(&loadModel)),"controller model loading function");
  check(getProc(instance,"xrStringToPath",reinterpret_cast<PFN_xrVoidFunction*>(&stringToPath)),"controller model paths function");
  check(stringToPath(instance,"/model_fb/controller/left",&paths[0]),"left controller model path");
  check(stringToPath(instance,"/model_fb/controller/right",&paths[1]),"right controller model path");
  // The runtime requires enumeration before properties may be queried, even
  // for the two standardized controller paths.
  PFN_xrEnumerateRenderModelPathsFB enumerate=nullptr;
  check(getProc(instance,"xrEnumerateRenderModelPathsFB",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate)),"enumerate model paths function");
  uint32_t count=0;check(enumerate(session,0,&count,nullptr),"runtime model path count");
  std::vector<XrRenderModelPathInfoFB> available(count,{XR_TYPE_RENDER_MODEL_PATH_INFO_FB});
  check(enumerate(session,count,&count,available.data()),"runtime model paths");
  for(auto& path:paths)if(std::none_of(available.begin(),available.end(),[&](const auto& item){return item.path==path;}))path=XR_NULL_PATH;
  LOG("[OpenXR] runtime controller paths: left %d right %d",int(paths[0]!=XR_NULL_PATH),int(paths[1]!=XR_NULL_PATH));
 }
 void refresh(XrTime time) {
  if(!properties||time<nextCheck)return;nextCheck=time+2000000000;
  for(int hand=0;hand<2;++hand)try {
   if(paths[hand]==XR_NULL_PATH)continue;
   XrRenderModelCapabilitiesRequestFB capabilities{XR_TYPE_RENDER_MODEL_CAPABILITIES_REQUEST_FB};capabilities.flags=XR_RENDER_MODEL_SUPPORTS_GLTF_2_0_SUBSET_1_BIT_FB;
   XrRenderModelPropertiesFB info{XR_TYPE_RENDER_MODEL_PROPERTIES_FB};info.next=&capabilities;
   XrResult result=properties(session,paths[hand],&info);
   if(result!=lastResult[hand]){LOG("[OpenXR] runtime %s model query result %d",hand?"right":"left",int(result));lastResult[hand]=result;}
   if(result==XR_RENDER_MODEL_UNAVAILABLE_FB||XR_FAILED(result)||info.modelKey==XR_NULL_RENDER_MODEL_KEY_FB||info.modelKey==loaded[hand]||info.modelKey==failed[hand])continue;
   failed[hand]=info.modelKey; // Retry a rejected asset only if the runtime replaces it.
   XrRenderModelLoadInfoFB request{XR_TYPE_RENDER_MODEL_LOAD_INFO_FB};request.modelKey=info.modelKey;
   XrRenderModelBufferFB buffer{XR_TYPE_RENDER_MODEL_BUFFER_FB};check(loadModel(session,&request,&buffer),"runtime model size");
   if(!buffer.bufferCountOutput||buffer.bufferCountOutput>32*1024*1024)throw std::runtime_error("Invalid runtime model size");
   std::vector<uint8_t> bytes(buffer.bufferCountOutput);buffer.bufferCapacityInput=uint32_t(bytes.size());buffer.buffer=bytes.data();
   check(loadModel(session,&request,&buffer),"load runtime controller model");
   TheaterMesh replacement;
   try {replacement.load_glb(bytes);}catch(...){replacement.destroy();throw;}
   models[hand].destroy();models[hand].parts.swap(replacement.parts);loaded[hand]=info.modelKey;failed[hand]=XR_NULL_RENDER_MODEL_KEY_FB;
   LOG("[OpenXR] runtime %s controller model loaded: %s (%zu bytes)",hand?"right":"left",info.modelName,bytes.size());
  }catch(const std::exception& error){LOG("[OpenXR] controller model unavailable: %s",error.what());nextCheck=time+10000000000;}
 }
 void destroy(){for(auto& model:models)model.destroy();}
};
