#pragma once
#include "controller_mapping.h"
#include <openxr/openxr.h>

namespace skate3::vr {
struct ControllerAim {
  bool valid=false;
  XrPosef pose{{0,0,0,1},{0,0,0}};
};
struct ControllerHands { ControllerAim aim[2]; };
class OpenXrGamepad {
 public:
  OpenXrGamepad(XrInstance instance,XrSession session);
  ~OpenXrGamepad();
  OpenXrGamepad(const OpenXrGamepad&)=delete;
  OpenXrGamepad& operator=(const OpenXrGamepad&)=delete;
  TouchControls Sample(bool focused);
  ControllerHands LocateHands(XrSpace base,XrTime display_time);
 private:
  enum Action { A,B,X,Y,Menu,LeftStick,RightStick,LeftClick,RightClick,
                LeftTrigger,RightTrigger,LeftGrip,RightGrip,LeftAim,RightAim,Count };
  XrSession session_;
  XrActionSet set_=XR_NULL_HANDLE;
  XrAction actions_[Count]{};
  XrSpace aim_spaces_[2]{};
  bool focused_=false;
};
}
