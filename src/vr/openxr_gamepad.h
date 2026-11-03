#pragma once
#include "controller_mapping.h"
#include <openxr/openxr.h>

namespace skate3::vr {
class OpenXrGamepad {
 public:
  OpenXrGamepad(XrInstance instance,XrSession session);
  ~OpenXrGamepad();
  OpenXrGamepad(const OpenXrGamepad&)=delete;
  OpenXrGamepad& operator=(const OpenXrGamepad&)=delete;
  TouchControls Sample(bool focused);
 private:
  enum Action { A,B,X,Y,Menu,LeftStick,RightStick,LeftClick,RightClick,
                LeftTrigger,RightTrigger,LeftGrip,RightGrip,Count };
  XrSession session_;
  XrActionSet set_=XR_NULL_HANDLE;
  XrAction actions_[Count]{};
};
}
