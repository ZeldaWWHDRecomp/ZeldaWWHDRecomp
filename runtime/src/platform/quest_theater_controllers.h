// Included inside quest_theater's private namespace. A small independent GLES
// layer displays tracked controllers; the game's Vulkan renderer is untouched.
struct TheaterControllers {
 static constexpr int extent=768;
 XrSession session=XR_NULL_HANDLE;
 XrSwapchain swapchain=XR_NULL_HANDLE;
 std::vector<XrSwapchainImageOpenGLESKHR> images;
 XrCompositionLayerProjectionView views[2]{};
 XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
 GLuint program=0,buffer=0,vao=0,fbo=0,depth=0;
 GLsizei vertices=0;
 PFN_xrCreateSwapchain createSwapchain=nullptr;
 PFN_xrDestroySwapchain destroySwapchain=nullptr;
 PFN_xrEnumerateSwapchainFormats formats=nullptr;
 PFN_xrEnumerateSwapchainImages enumerateImages=nullptr;
 PFN_xrAcquireSwapchainImage acquire=nullptr;
 PFN_xrWaitSwapchainImage wait=nullptr;
 PFN_xrReleaseSwapchainImage release=nullptr;
 PFN_xrLocateViews locate=nullptr;
 GLint eyePosition=-1,eyeRotation=-1,frustum=-1,position=-1,rotation=-1,scale=-1,color=-1;
 GLint primitiveMode=-1,useTexture=-1;

 static GLuint shader(GLenum type,const char* source) {
  GLuint result=glCreateShader(type);glShaderSource(result,1,&source,nullptr);glCompileShader(result);
  GLint ok=0;glGetShaderiv(result,GL_COMPILE_STATUS,&ok);
  if(!ok){char message[1024]{};glGetShaderInfoLog(result,sizeof(message),nullptr,message);glDeleteShader(result);
   throw std::runtime_error(std::string("Controller shader: ")+message);}
  return result;
 }
 void initialize(XrInstance instance,XrSession owner,PFN_xrGetInstanceProcAddr getProc) {
  session=owner;
  auto load=[&](auto& fn,const char* name){check(getProc(instance,name,reinterpret_cast<PFN_xrVoidFunction*>(&fn)),name);};
  load(createSwapchain,"xrCreateSwapchain");load(destroySwapchain,"xrDestroySwapchain");
  load(formats,"xrEnumerateSwapchainFormats");load(enumerateImages,"xrEnumerateSwapchainImages");
  load(acquire,"xrAcquireSwapchainImage");load(wait,"xrWaitSwapchainImage");
  load(release,"xrReleaseSwapchainImage");load(locate,"xrLocateViews");
  uint32_t count=0;check(formats(session,0,&count,nullptr),"controller formats");
  std::vector<int64_t> supported(count);check(formats(session,count,&count,supported.data()),"controller formats");
  const int64_t format=std::find(supported.begin(),supported.end(),GL_SRGB8_ALPHA8)!=supported.end()?GL_SRGB8_ALPHA8:GL_RGBA8;
  if(std::find(supported.begin(),supported.end(),format)==supported.end())throw std::runtime_error("Controller RGBA surface unavailable");
  XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
  info.format=format;info.sampleCount=1;info.width=info.height=extent;info.faceCount=1;info.arraySize=2;info.mipCount=1;
  check(createSwapchain(session,&info,&swapchain),"create controller layer");
  check(enumerateImages(swapchain,0,&count,nullptr),"controller image count");
  images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
  check(enumerateImages(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"controller images");
  const char* vertex=R"glsl(#version 300 es
   layout(location=0) in vec3 point;
   layout(location=1) in vec3 normal;
   layout(location=2) in vec2 texcoord;
   uniform vec3 eyePosition,position,scale;
   uniform vec4 eyeRotation,rotation,frustum;
   uniform bool primitiveMode;
   out float light;out vec2 uv;
   vec3 turn(vec4 q,vec3 v){return v+2.0*cross(q.xyz,cross(q.xyz,v)+q.w*v);}
   void main(){
    vec3 local=point,n=primitiveMode?point/scale:normal;
    vec3 world=position+turn(rotation,local*scale);
    vec3 p=turn(eyeRotation,world-eyePosition);
    float l=frustum.x,r=frustum.y,b=frustum.z,t=frustum.w;
    gl_Position=vec4(2.0*p.x/(r-l)+(r+l)*p.z/(r-l),
     2.0*p.y/(t-b)+(t+b)*p.z/(t-b),-1.002002*p.z-0.02002002,-p.z);
    light=0.6+0.4*max(0.0,dot(normalize(turn(rotation,n)),normalize(vec3(-0.3,0.8,0.5))));uv=texcoord;
   })glsl";
  const char* fragment=R"glsl(#version 300 es
   precision mediump float;
   uniform vec3 color;uniform bool useTexture;uniform sampler2D baseImage;
   in float light;in vec2 uv;out vec4 result;
   void main(){vec3 albedo=useTexture?texture(baseImage,uv).rgb:vec3(1.0);result=vec4(color*albedo*light,1.0);})glsl";
  GLuint vs=shader(GL_VERTEX_SHADER,vertex),fs=shader(GL_FRAGMENT_SHADER,fragment);
  program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
  glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
  if(!ok)throw std::runtime_error("Controller shader link failed");
  eyePosition=glGetUniformLocation(program,"eyePosition");eyeRotation=glGetUniformLocation(program,"eyeRotation");
  frustum=glGetUniformLocation(program,"frustum");position=glGetUniformLocation(program,"position");
  rotation=glGetUniformLocation(program,"rotation");scale=glGetUniformLocation(program,"scale");color=glGetUniformLocation(program,"color");
  primitiveMode=glGetUniformLocation(program,"primitiveMode");useTexture=glGetUniformLocation(program,"useTexture");
  std::vector<XrVector3f> mesh;
  auto point=[](int y,int x){float lat=3.14159265f*y/8,lon=6.2831853f*x/12;
   return XrVector3f{std::sin(lat)*std::cos(lon),std::cos(lat),std::sin(lat)*std::sin(lon)};};
  for(int y=0;y<8;++y)for(int x=0;x<12;++x){auto a=point(y,x),b=point(y+1,x),c=point(y+1,x+1),d=point(y,x+1);
   for(auto p:{a,b,c,a,c,d})mesh.push_back(p);}
  vertices=GLsizei(mesh.size());glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,mesh.size()*sizeof(XrVector3f),mesh.data(),GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(XrVector3f),nullptr);
  glGenFramebuffers(1,&fbo);glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);
  glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,extent,extent);
 }
 void shape(XrPosef pose,XrVector3f offset,XrVector3f size,XrVector3f tint) {
  glBindVertexArray(vao);glUniform1i(primitiveMode,1);glUniform1i(useTexture,0);
  auto p=translated(pose,offset);glUniform3f(position,p.position.x,p.position.y,p.position.z);
  glUniform4f(rotation,p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w);
  glUniform3f(scale,size.x,size.y,size.z);glUniform3f(color,tint.x,tint.y,tint.z);glDrawArrays(GL_TRIANGLES,0,vertices);
 }
 void model(const TheaterMesh& mesh,XrPosef pose) {
  glUniform1i(primitiveMode,0);
  glUniform3f(position,pose.position.x,pose.position.y,pose.position.z);
  glUniform4f(rotation,pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w);glUniform3f(scale,1,1,1);
  for(const auto& p:mesh.parts){glBindVertexArray(p.vao);glUniform3f(color,p.color[0],p.color[1],p.color[2]);
   glUniform1i(useTexture,p.texture!=0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,p.texture);
   glDrawElements(GL_TRIANGLES,p.count,GL_UNSIGNED_INT,nullptr);}
 }
 const XrCompositionLayerBaseHeader* draw(XrSpace space,XrTime time,const XrPosef (&grips)[2],
   const XrPosef (&rays)[2],const bool (&tracked)[2],const float (&lengths)[2],
   const TheaterRuntimeModels& models) {
  if(!tracked[0]&&!tracked[1])return nullptr;
  XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};locateInfo.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  locateInfo.displayTime=time;locateInfo.space=space;
  XrViewState state{XR_TYPE_VIEW_STATE};XrView located[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};uint32_t count=0;
  check(locate(session,&locateInfo,&state,2,&count,located),"locate controller views");
  constexpr auto valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
  if(count!=2||(state.viewStateFlags&valid)!=valid)return nullptr;
  uint32_t index=0;XrSwapchainImageAcquireInfo ac{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};check(acquire(swapchain,&ac,&index),"acquire controller image");
  XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;check(wait(swapchain,&wi),"wait controller image");
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
  glViewport(0,0,extent,extent);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
  glUseProgram(program);glBindVertexArray(vao);
  for(int eye=0;eye<2;++eye){
   glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,images[index].image,0,eye);
   if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Controller framebuffer incomplete");
   glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
   const auto& v=located[eye];auto q=inverse(v.pose.orientation);
   glUniform3f(eyePosition,v.pose.position.x,v.pose.position.y,v.pose.position.z);glUniform4f(eyeRotation,q.x,q.y,q.z,q.w);
   glUniform4f(frustum,std::tan(v.fov.angleLeft),std::tan(v.fov.angleRight),std::tan(v.fov.angleDown),std::tan(v.fov.angleUp));
   for(int hand=0;hand<2;++hand) {
    if(tracked[hand])model(models.models[hand],grips[hand]);
    if(tracked[hand]&&lengths[hand]>0) {
     float length=lengths[hand];shape(rays[hand],{0,0,-length*.5f},{.0015f,.0015f,length*.5f},{.18f,.6f,1.f});
     float radius=std::max(.004f,length*.0025f);shape(rays[hand],{0,0,-length},{radius,radius,radius},{.5f,.8f,1.f});
    }
   }
   views[eye]={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};views[eye].pose=v.pose;views[eye].fov=v.fov;
   views[eye].subImage={swapchain,{{0,0},{extent,extent}},uint32_t(eye)};
  }
  glBindFramebuffer(GL_FRAMEBUFFER,0);glFlush();
  XrSwapchainImageReleaseInfo re{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};check(release(swapchain,&re),"release controller image");
  layer.space=space;layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.viewCount=2;layer.views=views;
  return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
 }
 void destroy() noexcept {
  if(swapchain&&destroySwapchain)destroySwapchain(swapchain);
  if(program)glDeleteProgram(program);if(buffer)glDeleteBuffers(1,&buffer);if(vao)glDeleteVertexArrays(1,&vao);
  if(fbo)glDeleteFramebuffers(1,&fbo);if(depth)glDeleteRenderbuffers(1,&depth);
 }
};
