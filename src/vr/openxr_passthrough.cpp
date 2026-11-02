#include "openxr_passthrough.h"
#include <stdexcept>

namespace skate3::vr {
namespace {
void Check(XrResult result,const char* operation) {
  if (XR_FAILED(result)) throw std::runtime_error(std::string(operation)+": XR "+std::to_string(result));
}
template<class T> T Load(XrInstance instance,const char* name) {
  PFN_xrVoidFunction function=nullptr;
  Check(xrGetInstanceProcAddr(instance,name,&function),name);
  if (!function) throw std::runtime_error(std::string("Missing ")+name);
  return reinterpret_cast<T>(function);
}
}
OpenXrPassthrough::~OpenXrPassthrough() { Destroy(); }
void OpenXrPassthrough::Destroy() {
  if (layer_ && destroy_layer_) destroy_layer_(layer_);
  if (feature_ && destroy_feature_) destroy_feature_(feature_);
  layer_=XR_NULL_HANDLE;
  feature_=XR_NULL_HANDLE;
}
void OpenXrPassthrough::Disable() {
  Destroy();
  attempted_=false;
  error_.clear();
}
bool OpenXrPassthrough::Enable() {
  if (attempted_) return layer_!=XR_NULL_HANDLE;
  attempted_=true;
  if (!context_.passthrough_supported()) {
    error_=context_.runtime()=="Oculus"
        ? "Room view is unavailable. Enable Developer Runtime Features and Passthrough over Meta Quest Link in Settings > Beta, then restart the game."
        : "This headset or OpenXR runtime does not provide room view. The stereo window remains available.";
    return false;
  }
  try {
    const auto instance=context_.instance();
    destroy_feature_=Load<PFN_xrDestroyPassthroughFB>(instance,"xrDestroyPassthroughFB");
    destroy_layer_=Load<PFN_xrDestroyPassthroughLayerFB>(instance,"xrDestroyPassthroughLayerFB");
    auto create=Load<PFN_xrCreatePassthroughFB>(instance,"xrCreatePassthroughFB");
    auto create_layer=Load<PFN_xrCreatePassthroughLayerFB>(instance,"xrCreatePassthroughLayerFB");
    XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
    info.flags=XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    Check(create(session_,&info,&feature_),"Create passthrough");
    XrPassthroughLayerCreateInfoFB layer{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
    layer.passthrough=feature_;
    layer.flags=XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    layer.purpose=XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    Check(create_layer(session_,&layer,&layer_),"Create room-view layer");
    return true;
  } catch (const std::exception& error) {
    error_=error.what();
    Destroy();
    return false;
  }
}
}
