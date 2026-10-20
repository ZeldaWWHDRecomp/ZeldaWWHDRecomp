// The runtime owns the camera imagery. Submit its reconstruction layer behind
// the game and pause it completely when the user selects the black background.
struct TheaterPassthrough {
 XrPassthroughFB feature=XR_NULL_HANDLE;
 XrPassthroughLayerFB layer=XR_NULL_HANDLE;
 bool requested=false,running=false;
 PFN_xrCreatePassthroughFB create=nullptr;
 PFN_xrDestroyPassthroughFB destroyFeature=nullptr;
 PFN_xrCreatePassthroughLayerFB createLayer=nullptr;
 PFN_xrDestroyPassthroughLayerFB destroyLayer=nullptr;
 PFN_xrPassthroughStartFB start=nullptr;
 PFN_xrPassthroughPauseFB pause=nullptr;
 PFN_xrPassthroughLayerResumeFB resumeLayer=nullptr;
 PFN_xrPassthroughLayerPauseFB pauseLayer=nullptr;
 PFN_xrPassthroughLayerSetStyleFB setStyle=nullptr;

 bool available() const {return layer!=XR_NULL_HANDLE;}
 void initialize(XrInstance instance,XrSystemId system,XrSession session,PFN_xrGetInstanceProcAddr getProc) {
  try {
   auto load=[&](auto& fn,const char* name){check(getProc(instance,name,reinterpret_cast<PFN_xrVoidFunction*>(&fn)),name);};
   PFN_xrGetSystemProperties properties=nullptr;load(properties,"xrGetSystemProperties");
   XrSystemPassthroughPropertiesFB support{XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB};
   XrSystemProperties info{XR_TYPE_SYSTEM_PROPERTIES};info.next=&support;
   check(properties(instance,system,&info),"passthrough support");
   if(!support.supportsPassthrough)return;
   load(create,"xrCreatePassthroughFB");load(destroyFeature,"xrDestroyPassthroughFB");
   load(createLayer,"xrCreatePassthroughLayerFB");load(destroyLayer,"xrDestroyPassthroughLayerFB");
   load(start,"xrPassthroughStartFB");load(pause,"xrPassthroughPauseFB");
   load(resumeLayer,"xrPassthroughLayerResumeFB");load(pauseLayer,"xrPassthroughLayerPauseFB");
   load(setStyle,"xrPassthroughLayerSetStyleFB");
   XrPassthroughCreateInfoFB featureInfo{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
   check(create(session,&featureInfo,&feature),"create paused passthrough");
   XrPassthroughLayerCreateInfoFB layerInfo{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
   layerInfo.passthrough=feature;layerInfo.purpose=XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
   check(createLayer(session,&layerInfo,&layer),"create paused passthrough layer");
   XrPassthroughStyleFB style{XR_TYPE_PASSTHROUGH_STYLE_FB};style.textureOpacityFactor=1.f;
   check(setStyle(layer,&style),"set passthrough style");
   LOG("[OpenXR] passthrough available; starts with black background");
  }catch(const std::exception& error){LOG("[OpenXR] passthrough unavailable: %s",error.what());destroy();}
 }
 void update(bool visible) {
  const bool wanted=requested&&visible&&available();
  if(wanted==running)return;
  if(wanted) {
   XrResult result=start(feature);
   if(XR_SUCCEEDED(result)) {
    result=resumeLayer(layer);
    if(XR_FAILED(result))pause(feature);
   }
   if(XR_FAILED(result)){requested=false;LOG("[OpenXR] passthrough could not start (%d)",int(result));return;}
   running=true;
  } else {
   const XrResult layerResult=pauseLayer(layer),featureResult=pause(feature);
   if(XR_FAILED(layerResult)||XR_FAILED(featureResult))LOG("[OpenXR] passthrough pause results %d / %d",int(layerResult),int(featureResult));
   running=false;
  }
  LOG("[OpenXR] passthrough %s",running?"running":"paused");
 }
 void destroy() noexcept {
  if(layer&&destroyLayer)destroyLayer(layer);layer=XR_NULL_HANDLE;
  if(feature&&destroyFeature)destroyFeature(feature);feature=XR_NULL_HANDLE;
  requested=running=false;
 }
};
