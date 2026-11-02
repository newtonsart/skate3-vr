#pragma once
#include "openxr_context.h"

namespace skate3::vr {
class OpenXrPassthrough {
 public:
  OpenXrPassthrough(const OpenXrContext& context, XrSession session)
      : context_(context),session_(session) {}
  ~OpenXrPassthrough();
  OpenXrPassthrough(const OpenXrPassthrough&)=delete;
  OpenXrPassthrough& operator=(const OpenXrPassthrough&)=delete;
  bool Enable();
  void Disable();
  XrPassthroughLayerFB layer() const { return layer_; }
  const std::string& error() const { return error_; }

 private:
  void Destroy();
  const OpenXrContext& context_;
  XrSession session_;
  XrPassthroughFB feature_=XR_NULL_HANDLE;
  XrPassthroughLayerFB layer_=XR_NULL_HANDLE;
  PFN_xrDestroyPassthroughFB destroy_feature_=nullptr;
  PFN_xrDestroyPassthroughLayerFB destroy_layer_=nullptr;
  bool attempted_=false;
  std::string error_;
};
}
