#include "quest_theater.h"
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <android/native_window_jni.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>
#include <array>
#include <memory>
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include "../../third_party/stb/stb_image.h"
#include "../runtime.h"
#include "../gx2/gx2.h"
#include "../gfx/stereo.h"
#include "../overlay/hostui.h"

namespace quest_theater {
namespace {
constexpr int panelWidth=1024, panelHeight=256;
XrVector3f add(XrVector3f a,XrVector3f b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
XrVector3f mul(XrVector3f a,float s){return {a.x*s,a.y*s,a.z*s};}
XrVector3f cross(XrVector3f a,XrVector3f b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
XrVector3f rotate(XrQuaternionf q,XrVector3f v) {
 const XrVector3f u{q.x,q.y,q.z};return add(v,mul(add(mul(cross(u,v),q.w),cross(u,cross(u,v))),2));
}
XrQuaternionf inverse(XrQuaternionf q){return {-q.x,-q.y,-q.z,q.w};}
XrPosef translated(XrPosef p,XrVector3f v){p.position=add(p.position,rotate(p.orientation,v));return p;}
bool java_flag(const char* name) {
 auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
 auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
 if(!env||!activity)return false;
 auto cls=env->GetObjectClass(activity);auto method=env->GetMethodID(cls,name,"()Z");
 bool result=method&&env->CallBooleanMethod(activity,method);
 if(env->ExceptionCheck()){env->ExceptionClear();result=false;}
 env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);return result;
}
void check(XrResult result,const char* operation) {
 if(XR_FAILED(result))throw std::runtime_error(std::string(operation)+" failed ("+std::to_string(result)+")");
}
#include "quest_theater_models.h"
#include "quest_theater_controllers.h"
#include "quest_theater_passthrough.h"
#define XR_FUNCTIONS(X) \
 X(EnumerateInstanceExtensionProperties) X(CreateInstance) X(DestroyInstance) X(GetSystem) \
 X(CreateSession) X(DestroySession) X(CreateReferenceSpace) X(DestroySpace) X(PollEvent) \
 X(BeginSession) X(EndSession) X(WaitFrame) X(BeginFrame) X(EndFrame) X(LocateSpace) \
 X(CreateActionSet) X(DestroyActionSet) X(CreateAction) X(StringToPath) \
 X(SuggestInteractionProfileBindings) X(AttachSessionActionSets) X(CreateActionSpace) \
 X(SyncActions) X(GetActionStateBoolean) X(GetActionStateFloat) X(GetActionStateVector2f) X(GetActionStatePose) X(DestroySwapchain)
struct Theater {
 JavaVM* vm=nullptr; jobject activity=nullptr; jclass activityClass=nullptr;
 JNIEnv* env=nullptr; jmethodID paint=nullptr;
 void* loader=nullptr; PFN_xrGetInstanceProcAddr getProc=nullptr;
#define DECLARE(name) PFN_xr##name name=nullptr;
 XR_FUNCTIONS(DECLARE)
#undef DECLARE
 PFN_xrCreateSwapchainAndroidSurfaceKHR createSurface=nullptr;
 XrInstance instance=XR_NULL_HANDLE; XrSystemId system=XR_NULL_SYSTEM_ID;
 XrSession session=XR_NULL_HANDLE; XrSpace local=XR_NULL_HANDLE,view=XR_NULL_HANDLE;
 XrSwapchain screen=XR_NULL_HANDLE,panel=XR_NULL_HANDLE;
 jobject screenSurface=nullptr,panelSurface=nullptr;
 ANativeWindow* native=nullptr;
 EGLDisplay display=EGL_NO_DISPLAY; EGLContext context=EGL_NO_CONTEXT; EGLSurface pbuffer=EGL_NO_SURFACE;
 std::atomic<bool> showing{false}; std::atomic<int> packing{-1};
 bool running=false,cylinderSupported=false,curved=false,placed=false,panelOpen=false,panelVisible=false;
 float screenWidth=3.2f,distance=2.6f;
 XrPosef pose{{0,0,0,1},{0,0,-2.6f}};
 XrActionSet actions=XR_NULL_HANDLE; XrAction aim=XR_NULL_HANDLE,trigger=XR_NULL_HANDLE;
 XrAction grip=XR_NULL_HANDLE,squeeze=XR_NULL_HANDLE,stick=XR_NULL_HANDLE,menu=XR_NULL_HANDLE;
 XrPath hands[2]{}; XrSpace aims[2]{},gripSpaces[2]{}; bool held[2]{},squeezed[2]{},menuHeld=false;
 XrPosef gripPoses[2]{},aimPoses[2]{};bool tracked[2]{};float rayLengths[2]{};
 int grabHand=-1;float grabDistance=0;XrVector3f grabOffset{};XrTime previousInputTime=0;
 TheaterControllers controllers;
 TheaterRuntimeModels runtimeModels;
 TheaterPassthrough passthrough;
 int paintedHover=-2,paintedState=-1;

 // Startup failure happens before Vulkan is given the surface. Successful
 // sessions retain these resources across pause/resume until process shutdown.
 void failed_start_cleanup() noexcept {
  if(native){ANativeWindow_release(native);native=nullptr;}
  controllers.destroy();
  runtimeModels.destroy();
  passthrough.destroy();
  if(DestroySpace) { for(auto space:aims)if(space)DestroySpace(space);for(auto space:gripSpaces)if(space)DestroySpace(space);if(view)DestroySpace(view);if(local)DestroySpace(local); }
  if(DestroySwapchain){if(panel)DestroySwapchain(panel);if(screen)DestroySwapchain(screen);}
  if(DestroySession&&session)DestroySession(session);
  if(DestroyActionSet&&actions)DestroyActionSet(actions);
  if(DestroyInstance&&instance)DestroyInstance(instance);
  if(display!=EGL_NO_DISPLAY){eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
   if(pbuffer!=EGL_NO_SURFACE)eglDestroySurface(display,pbuffer);
   if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);eglTerminate(display);}
  if(env){if(screenSurface)env->DeleteLocalRef(screenSurface);if(panelSurface)env->DeleteLocalRef(panelSurface);}
 }

 template<class T> void load(T& fn,const char* name,XrInstance owner) {
  check(getProc(owner,name,reinterpret_cast<PFN_xrVoidFunction*>(&fn)),name);
 }
 bool extension(const std::vector<XrExtensionProperties>& list,const char* name) {
  return std::any_of(list.begin(),list.end(),[&](const auto& e){return !strcmp(e.extensionName,name);});
 }
 XrPath path(const char* value){XrPath out;check(StringToPath(instance,value,&out),"xrStringToPath");return out;}
 void create_egl() {
  display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if(display==EGL_NO_DISPLAY||!eglInitialize(display,nullptr,nullptr))throw std::runtime_error("Theater EGL initialization failed");
  EGLint attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
      EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig config=nullptr;EGLint count=0;
  if(!eglChooseConfig(display,attributes,&config,1,&count)||!count)throw std::runtime_error("Theater EGL configuration unavailable");
  EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);
  EGLint surfaceAttributes[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
  pbuffer=eglCreatePbufferSurface(display,config,surfaceAttributes);
  if(context==EGL_NO_CONTEXT||pbuffer==EGL_NO_SURFACE||!eglMakeCurrent(display,pbuffer,pbuffer,context))
   throw std::runtime_error("Theater EGL context unavailable");
  PFN_xrGetOpenGLESGraphicsRequirementsKHR requirements=nullptr;
  load(requirements,"xrGetOpenGLESGraphicsRequirementsKHR",instance);
  XrGraphicsRequirementsOpenGLESKHR supported{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
  check(requirements(instance,system,&supported),"OpenXR GLES requirements");
  GLint major=0,minor=0;glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
  const auto version=XR_MAKE_VERSION(major,minor,0);
  if(version<supported.minApiVersionSupported||version>supported.maxApiVersionSupported)
   throw std::runtime_error("Theater EGL version is outside the OpenXR runtime requirements");
  XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
  binding.display=display;binding.config=config;binding.context=context;
  XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};info.next=&binding;info.systemId=system;
  check(CreateSession(instance,&info,&session),"xrCreateSession");
 }
 void make_surface(XrSwapchain& swapchain,jobject& surface,int w,int h) {
  // Android producer swapchains require format/sample/face/array/mip counts zero.
  XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.width=w;info.height=h;
  check(createSurface(session,&info,&swapchain,&surface),"xrCreateSwapchainAndroidSurfaceKHR");
 }
 void create_actions() {
  hands[0]=path("/user/hand/left");hands[1]=path("/user/hand/right");
  XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
  strcpy(set.actionSetName,"theater");strcpy(set.localizedActionSetName,"Floating screen");
  check(CreateActionSet(instance,&set,&actions),"xrCreateActionSet");
  XrActionCreateInfo action{XR_TYPE_ACTION_CREATE_INFO};action.countSubactionPaths=2;action.subactionPaths=hands;
  action.actionType=XR_ACTION_TYPE_POSE_INPUT;strcpy(action.actionName,"point");strcpy(action.localizedActionName,"Point at controls");
  check(CreateAction(actions,&action,&aim),"create pointing action");
  strcpy(action.actionName,"grip_pose");strcpy(action.localizedActionName,"Controller pose");
  check(CreateAction(actions,&action,&grip),"create grip pose action");
  action.actionType=XR_ACTION_TYPE_FLOAT_INPUT;strcpy(action.actionName,"trigger");strcpy(action.localizedActionName,"Select control");
  check(CreateAction(actions,&action,&trigger),"create trigger action");
  strcpy(action.actionName,"squeeze");strcpy(action.localizedActionName,"Grab screen");
  check(CreateAction(actions,&action,&squeeze),"create squeeze action");
  action.actionType=XR_ACTION_TYPE_VECTOR2F_INPUT;strcpy(action.actionName,"adjust");strcpy(action.localizedActionName,"Screen size and distance");
  check(CreateAction(actions,&action,&stick),"create adjustment action");
  action.actionType=XR_ACTION_TYPE_BOOLEAN_INPUT;action.countSubactionPaths=0;action.subactionPaths=nullptr;
  strcpy(action.actionName,"menu");strcpy(action.localizedActionName,"Screen controls");
  check(CreateAction(actions,&action,&menu),"create controls menu action");
  XrActionSuggestedBinding bindings[]={{aim,path("/user/hand/left/input/aim/pose")},
    {aim,path("/user/hand/right/input/aim/pose")},{trigger,path("/user/hand/left/input/trigger/value")},
    {trigger,path("/user/hand/right/input/trigger/value")},
    {grip,path("/user/hand/left/input/grip/pose")},{grip,path("/user/hand/right/input/grip/pose")},
    {squeeze,path("/user/hand/left/input/squeeze/value")},{squeeze,path("/user/hand/right/input/squeeze/value")},
    {stick,path("/user/hand/left/input/thumbstick")},{stick,path("/user/hand/right/input/thumbstick")},
    {menu,path("/user/hand/left/input/menu/click")}};
  XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  suggested.interactionProfile=path("/interaction_profiles/oculus/touch_controller");
  suggested.countSuggestedBindings=uint32_t(std::size(bindings));suggested.suggestedBindings=bindings;
  check(SuggestInteractionProfileBindings(instance,&suggested),"suggest Quest controls");
  XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&actions;
  check(AttachSessionActionSets(session,&attach),"attach Quest controls");
  for(int i=0;i<2;++i){XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};info.action=aim;
   info.subactionPath=hands[i];info.poseInActionSpace.orientation.w=1;
   check(CreateActionSpace(session,&info,&aims[i]),"create pointer space");
   info.action=grip;check(CreateActionSpace(session,&info,&gripSpaces[i]),"create grip space");}
 }
 void setup() {
  loader=dlopen("libopenxr_loader.so",RTLD_NOW|RTLD_LOCAL);
  if(!loader)throw std::runtime_error("OpenXR Android loader is unavailable");
  getProc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(loader,"xrGetInstanceProcAddr"));
  if(!getProc)throw std::runtime_error("OpenXR loader entry point is unavailable");
  PFN_xrInitializeLoaderKHR initializeLoader=nullptr;load(initializeLoader,"xrInitializeLoaderKHR",XR_NULL_HANDLE);
  XrLoaderInitInfoAndroidKHR init{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};init.applicationVM=vm;init.applicationContext=activity;
  check(initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&init)),"initialize Android OpenXR loader");
  load(EnumerateInstanceExtensionProperties,"xrEnumerateInstanceExtensionProperties",XR_NULL_HANDLE);
  load(CreateInstance,"xrCreateInstance",XR_NULL_HANDLE);
  uint32_t count=0;check(EnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr),"enumerate XR extensions");
  std::vector<XrExtensionProperties> available(count,{XR_TYPE_EXTENSION_PROPERTIES});
  check(EnumerateInstanceExtensionProperties(nullptr,count,&count,available.data()),"read XR extensions");
  std::vector<const char*> extensions={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
    XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME,
    XR_FB_COMPOSITION_LAYER_IMAGE_LAYOUT_EXTENSION_NAME};
  for(auto name:extensions)if(!extension(available,name))throw std::runtime_error(std::string("OpenXR runtime missing ")+name);
  cylinderSupported=extension(available,XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
  if(cylinderSupported)extensions.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
  const bool passthroughSupported=extension(available,XR_FB_PASSTHROUGH_EXTENSION_NAME);
  if(passthroughSupported)extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
  const bool modelsSupported=extension(available,XR_FB_RENDER_MODEL_EXTENSION_NAME);
  if(modelsSupported)extensions.push_back(XR_FB_RENDER_MODEL_EXTENSION_NAME);
  XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};android.applicationVM=vm;android.applicationActivity=activity;
  XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};info.next=&android;
  strcpy(info.applicationInfo.applicationName,"Wind Waker HD Theater");info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,34);
  info.enabledExtensionCount=uint32_t(extensions.size());info.enabledExtensionNames=extensions.data();
  check(CreateInstance(&info,&instance),"xrCreateInstance");
#define LOAD(name) load(name,"xr" #name,instance);
  XR_FUNCTIONS(LOAD)
#undef LOAD
  load(createSurface,"xrCreateSwapchainAndroidSurfaceKHR",instance);
  XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  check(GetSystem(instance,&systemInfo,&system),"xrGetSystem");
  create_egl();
  XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.poseInReferenceSpace.orientation.w=1;
  space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;check(CreateReferenceSpace(session,&space,&local),"create local space");
  space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;check(CreateReferenceSpace(session,&space,&view),"create view space");
  make_surface(screen,screenSurface,width,height);make_surface(panel,panelSurface,panelWidth,panelHeight);
  native=ANativeWindow_fromSurface(env,screenSurface);
  if(!native)throw std::runtime_error("OpenXR did not provide a game surface");
  create_actions();
  controllers.initialize(instance,session,getProc);
  if(modelsSupported)runtimeModels.initialize(instance,session,getProc);
  LOG("[OpenXR] runtime controller models %s",modelsSupported?"supported":"unavailable");
  if(passthroughSupported)passthrough.initialize(instance,system,session,getProc);
  LOG("[OpenXR] floating screen initialized; curved screen %s; starts in 2D",cylinderSupported?"available":"unavailable");
 }
 void center(const XrPosef& head) {
  const auto forward=rotate(head.orientation,{0,0,-1});
  const float yaw=std::atan2(-forward.x,-forward.z);
  pose.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
  pose.position=add(head.position,rotate(pose.orientation,{0,-.1f,-distance}));placed=true;
 }
 XrPosef panel_pose() const{return translated(pose,{0,-screenWidth*9.f/32.f-(panelOpen?.21f:.08f),.05f});}
 float panel_width() const{return panelOpen?1.35f:.32f;}
 float intersect(const XrPosef& ray,XrPosef target,float w,float h,float* u=nullptr,float* v=nullptr) const {
  const auto inv=inverse(target.orientation);
  const auto origin=rotate(inv,add(ray.position,mul(target.position,-1)));
  const auto direction=rotate(inv,rotate(ray.orientation,{0,0,-1}));
  if(direction.z>=-0.0001f)return -1;
  const float t=-origin.z/direction.z;if(t<0)return -1;
  const auto point=add(origin,mul(direction,t));const float x=point.x/w+.5f,y=.5f-point.y/h;
  if(x<0||x>=1||y<0||y>=1)return -1;
  if(u)*u=x;if(v)*v=y;return t;
 }
 float screen_hit(const XrPosef& ray) const {
  if(!curved)return intersect(ray,pose,screenWidth,screenWidth*9.f/16.f);
  auto inv=inverse(pose.orientation);
  auto o=rotate(inv,add(ray.position,mul(pose.position,-1)));
  auto d=rotate(inv,rotate(ray.orientation,{0,0,-1}));
  float radius=screenWidth/1.15f,z=o.z-radius,a=d.x*d.x+d.z*d.z;
  if(a<1e-6f)return -1;
  float b=2*(o.x*d.x+z*d.z),c=o.x*o.x+z*z-radius*radius,disc=b*b-4*a*c;
  if(disc<0)return -1;
  for(float t:{(-b-std::sqrt(disc))/(2*a),(-b+std::sqrt(disc))/(2*a)})if(t>0){
   auto p=add(o,mul(d,t));if(std::abs(std::atan2(p.x,radius-p.z))<=.575f&&std::abs(p.y)<=screenWidth*9.f/32.f)return t;
  }
  return -1;
 }
 int hit(const XrPosef& ray,float& distanceToPanel) const {
  float u=0,v=0,w=panel_width();distanceToPanel=intersect(ray,panel_pose(),w,w*panelHeight/panelWidth,&u,&v);
  if(distanceToPanel<0)return -1;
  if(!panelOpen)return 5;
  return v>=.08f&&v<.60f?std::min(4,int(u*5)):-1;
 }
 void button(int selected,const XrPosef& head) {
  switch(selected) {
   case 0:{const int mode=stereo::requestedMode.load()==stereo::Off?stereo::HalfSBS:stereo::Off;
    hostui::post([mode]{stereo::set_mode(mode);});break;}
   case 1:if(cylinderSupported)curved=!curved;break;
   case 2:if(passthrough.available())passthrough.requested=!passthrough.requested;break;
   case 3:center(head);grabHand=-1;break;
   case 4:panelOpen=false;break;
   case 5:panelOpen=true;break;
  }
  LOG("[OpenXR] control %d, width %.2f distance %.2f curved %d panel %d",selected,screenWidth,distance,int(curved),int(panelOpen));
 }
 void input(XrTime time,const XrPosef& head,bool focused) {
  int hover=-1;
  const float dt=previousInputTime?std::clamp(float(time-previousInputTime)*1e-9f,0.f,.05f):0.f;previousInputTime=time;
  tracked[0]=tracked[1]=false;rayLengths[0]=rayLengths[1]=0;
  if(focused) {
   runtimeModels.refresh(time);
   XrActiveActionSet active{actions,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
   check(SyncActions(session,&sync),"xrSyncActions");
   XrActionStateGetInfo menuGet{XR_TYPE_ACTION_STATE_GET_INFO};menuGet.action=menu;
   XrActionStateBoolean menuState{XR_TYPE_ACTION_STATE_BOOLEAN};check(GetActionStateBoolean(session,&menuGet,&menuState),"read menu button");
   bool menuDown=menuState.isActive&&menuState.currentState;
   if(menuDown&&!menuHeld)panelOpen=!panelOpen;menuHeld=menuDown;
   bool pressed[2]{},grabPressed[2]{};XrVector2f adjustment[2]{};
   for(int hand=1;hand>=0;--hand) {
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.subactionPath=hands[hand];get.action=trigger;
    XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};check(GetActionStateFloat(session,&get,&value),"read trigger");
    bool triggerDown=value.isActive&&value.currentState>.7f;
    get.action=squeeze;check(GetActionStateFloat(session,&get,&value),"read grip");
    bool squeezeDown=value.isActive&&value.currentState>.6f;
    get.action=stick;XrActionStateVector2f axes{XR_TYPE_ACTION_STATE_VECTOR2F};check(GetActionStateVector2f(session,&get,&axes),"read adjustment stick");
    if(axes.isActive)adjustment[hand]=axes.currentState;
    auto locatePose=[&](XrAction action,XrSpace space,XrPosef& pose){
     get.action=action;XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};check(GetActionStatePose(session,&get,&state),"read controller pose state");
     if(!state.isActive)return false;
     XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};check(LocateSpace(space,local,time,&location),"locate controller");
     constexpr auto valid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
     if((location.locationFlags&valid)!=valid)return false;pose=location.pose;return true;
    };
    tracked[hand]=locatePose(aim,aims[hand],aimPoses[hand])&&locatePose(grip,gripSpaces[hand],gripPoses[hand]);
    pressed[hand]=triggerDown&&!held[hand];held[hand]=triggerDown;
    grabPressed[hand]=squeezeDown&&!squeezed[hand];squeezed[hand]=squeezeDown;
   }
   if(grabHand>=0&&(!tracked[grabHand]||!squeezed[grabHand])){
    LOG("[OpenXR] screen placed: width %.2f distance %.2f",screenWidth,distance);grabHand=-1;
   }
   bool clicked=false;
   for(int hand=1;hand>=0;--hand)if(tracked[hand]) {
    const auto& ray=aimPoses[hand];float panelDistance=-1;int target=hit(ray,panelDistance);
    float screenDistance=screen_hit(ray),at=panelDistance>0?panelDistance:screenDistance;
    if(grabHand<0&&grabPressed[hand]&&at>0) {
     grabHand=hand;grabDistance=at;grabOffset=add(pose.position,mul(translated(ray,{0,0,-at}).position,-1));
     panelOpen=false;
    }
    if(grabHand==hand) {
     auto axes=adjustment[hand];
     if(std::abs(axes.x)>.2f)screenWidth=std::clamp(screenWidth+axes.x*1.5f*dt,1.2f,5.f);
     if(std::abs(axes.y)>.2f)grabDistance=std::clamp(grabDistance+axes.y*2.f*dt,.5f,5.f);
     pose.position=add(translated(ray,{0,0,-grabDistance}).position,grabOffset);
     auto relative=add(pose.position,mul(head.position,-1));distance=std::clamp(std::sqrt(relative.x*relative.x+relative.y*relative.y+relative.z*relative.z),1.2f,5.f);
     rayLengths[hand]=grabDistance;
    } else {
     rayLengths[hand]=at>0?at:0;
     if(target>=0&&hover<0)hover=target;
     if(target>=0&&pressed[hand]&&!clicked){button(target,head);clicked=true;}
    }
   }
  } else {held[0]=held[1]=squeezed[0]=squeezed[1]=false;menuHeld=false;grabHand=-1;}
  // Hit testing stays active while hidden, so pointing below the screen reveals
  // a fully opaque handle. An opened panel remains until explicitly dismissed.
  panelVisible=focused&&grabHand<0&&(panelOpen||hover==5);
  const int state=(stereo::requestedMode.load()!=stereo::Off?1:0)|(curved?2:0)|(grabHand>=0?4:0)|(cylinderSupported?8:0)|(panelOpen?16:0)|
   (passthrough.available()?32:0)|(passthrough.requested?64:0);
  if(showing.load()&&panelVisible&&(hover!=paintedHover||state!=paintedState)) {
   env->CallStaticVoidMethod(activityClass,paint,panelSurface,hover,state);
   if(env->ExceptionCheck()){env->ExceptionDescribe();env->ExceptionClear();throw std::runtime_error("Theater control surface drawing failed");}
   paintedHover=hover;paintedState=state;
  }
 }
 void loop() {
  XrSessionState state=XR_SESSION_STATE_UNKNOWN;
  for(;;) {
   XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
   while(PollEvent(instance,&event)==XR_SUCCESS) {
    if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)throw std::runtime_error("OpenXR instance lost");
    if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)placed=false;
    if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
     state=reinterpret_cast<XrEventDataSessionStateChanged&>(event).state;
     const bool visibleNow=state==XR_SESSION_STATE_VISIBLE||state==XR_SESSION_STATE_FOCUSED;
     const bool wasVisible=showing.exchange(visibleNow);
     LOG("[OpenXR] session state %d",int(state));
     passthrough.update(visibleNow);
     // Gate new Vulkan presents, then drain under the GX2 execution lock.
     // The Android-surface extension forbids writes after xrEndSession.
     if(wasVisible&&!visibleNow)gx2::quiesce_quest_presentation();
     if(state==XR_SESSION_STATE_READY){XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
      check(BeginSession(session,&begin),"xrBeginSession");running=true;}
     if(state==XR_SESSION_STATE_STOPPING){gx2::quiesce_quest_presentation();check(EndSession(session),"xrEndSession");running=false;}
     if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)return;
    }
    event={XR_TYPE_EVENT_DATA_BUFFER};
   }
   if(!running){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
   XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame{XR_TYPE_FRAME_STATE};check(WaitFrame(session,&wait,&frame),"xrWaitFrame");
   // Apply toggles before constructing this frame's layer list. Input below may
   // request the next frame's state, but must not pause an already-listed layer.
   passthrough.update(frame.shouldRender&&showing.load());
   XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};check(BeginFrame(session,&begin),"xrBeginFrame");
   XrCompositionLayerQuad quads[3]{};XrCompositionLayerCylinderKHR cylinders[2]{};
   // Vulkan WSI and Canvas produce top-down Android buffers. This GLES-bound
   // XR session otherwise interprets both surfaces bottom-up. Correct only
   // compositor sampling, including curved screens and the control panel.
   XrCompositionLayerImageLayoutFB imageLayout{XR_TYPE_COMPOSITION_LAYER_IMAGE_LAYOUT_FB};
   imageLayout.flags=XR_COMPOSITION_LAYER_IMAGE_LAYOUT_VERTICAL_FLIP_BIT_FB;
   XrCompositionLayerPassthroughFB surroundings{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
   const XrCompositionLayerBaseHeader* layers[5]{};uint32_t count=0;
   if(frame.shouldRender&&showing.load()) {
    // Put the room beneath the opaque game screen and optional controls.
    if(passthrough.running) {
     surroundings.flags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
     surroundings.layerHandle=passthrough.layer;
     layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&surroundings);
    }
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};check(LocateSpace(view,local,frame.predictedDisplayTime,&head),"locate head");
    constexpr auto valid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if((head.locationFlags&valid)==valid) {
     if(!placed)center(head.pose);
     input(frame.predictedDisplayTime,head.pose,state==XR_SESSION_STATE_FOCUSED);
     const int mode=packing.load();const int eyes=mode>stereo::Off?2:1;
     if(mode>=0)for(int eye=0;eye<eyes;++eye) {
      XrSwapchainSubImage image{screen,{{0,0},{width,height}},0};
      if(mode==stereo::TAB){image.imageRect.extent.height=height/2;image.imageRect.offset.y=eye*height/2;}
      else if(eyes==2){image.imageRect.extent.width=width/2;image.imageRect.offset.x=eye*width/2;}
      const auto visibility=eyes==1?XR_EYE_VISIBILITY_BOTH:eye==0?XR_EYE_VISIBILITY_LEFT:XR_EYE_VISIBILITY_RIGHT;
      if(curved) {
       auto& layer=cylinders[eye];layer.type=XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;layer.next=&imageLayout;layer.space=local;layer.eyeVisibility=visibility;layer.subImage=image;
       layer.centralAngle=1.15f;layer.radius=screenWidth/layer.centralAngle;layer.aspectRatio=16.f/9.f;
       layer.pose=translated(pose,{0,0,layer.radius});layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
      } else {
       auto& layer=quads[eye];layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.next=&imageLayout;layer.space=local;layer.eyeVisibility=visibility;layer.subImage=image;
       layer.pose=pose;layer.size={screenWidth,screenWidth*9.f/16.f};layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
      }
     }
     if(panelVisible) {
     auto& toolbar=quads[2];toolbar.type=XR_TYPE_COMPOSITION_LAYER_QUAD;toolbar.next=&imageLayout;toolbar.space=local;toolbar.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
     toolbar.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
     toolbar.subImage={panel,{{0,0},{panelWidth,panelHeight}},0};toolbar.pose=panel_pose();toolbar.size={panel_width(),panel_width()*panelHeight/panelWidth};
     layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&toolbar);
     }
     if(auto* controllerLayer=controllers.draw(local,frame.predictedDisplayTime,gripPoses,aimPoses,tracked,rayLengths,runtimeModels))layers[count++]=controllerLayer;
    }
   }
   XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
   end.layerCount=count;end.layers=layers;check(EndFrame(session,&end),"xrEndFrame");
  }
 }
};
// Session/surfaces stay alive across Android pause/resume, just like the SDL
// renderer. The app's existing orderly native shutdown ends this process.
Theater* theater=nullptr;
}
bool active(){return theater!=nullptr;}
bool visible(){return theater&&theater->showing.load();}
ANativeWindow* window(){return theater?theater->native:nullptr;}
void presented(int mode){if(theater)theater->packing=mode;}
void initialize() {
 if(theater||!java_flag("useQuestTheater"))return;
 auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
 auto* host=new Theater;env->GetJavaVM(&host->vm);host->activity=env->NewGlobalRef(activity);
 auto cls=env->GetObjectClass(activity);host->activityClass=static_cast<jclass>(env->NewGlobalRef(cls));
 host->paint=env->GetStaticMethodID(cls,"paintTheaterControls","(Landroid/view/Surface;II)V");
 env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);
 if(!host->paint) {
  if(env->ExceptionCheck())env->ExceptionClear();
  env->DeleteGlobalRef(host->activityClass);env->DeleteGlobalRef(host->activity);delete host;
  LOG("[OpenXR] Quest control bar is unavailable; using the normal Android view");return;
 }
 std::promise<void> started;auto ready=started.get_future();
 std::thread worker([host,started=std::move(started)]() mutable {
  bool initialized=false;
  try {
   if(host->vm->AttachCurrentThread(&host->env,nullptr)!=JNI_OK)throw std::runtime_error("OpenXR thread could not attach to Android");
   host->setup();initialized=true;started.set_value();host->loop();
  }catch(const std::exception& error){LOG("[OpenXR] %s",error.what());if(!initialized){host->failed_start_cleanup();started.set_exception(std::current_exception());}}
  host->showing=false;
  host->passthrough.update(false);
  if(initialized)gx2::quiesce_quest_presentation();
  if(host->env)host->vm->DetachCurrentThread();
 });
 try { ready.get(); }
 catch(const std::exception& error) {
  // No Vulkan surface has been handed out yet. Wait for startup cleanup before
  // releasing the JNI/loader owners, then let SDL supply its ordinary surface.
  worker.join();
  env->DeleteGlobalRef(host->activityClass);env->DeleteGlobalRef(host->activity);
  if(host->loader)dlclose(host->loader);
  delete host;
  LOG("[OpenXR] %s; using the normal Android view",error.what());return;
 }
 theater=host;worker.detach();
}
}
