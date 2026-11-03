#pragma once
#include "stereo_math.h"
#include <cstdint>

namespace skate3::vr {
struct WindowFacing {
  XrQuaternionf rotation{0,0,0,1}, original{0,0,0,1};
  bool ready=false, editing=false;
  uint64_t applied_revision=0;
  XrPosef Update(XrPosef pose,XrVector3f head,bool face_viewer,bool placing,bool holding,uint64_t applied) {
    if(!ready) {
      rotation=face_viewer ? FaceWindowToHead(pose,head).orientation : pose.orientation;
      ready=true;
    }
    if(placing && !editing) { original=rotation; applied_revision=applied; }
    if(!placing && editing && applied==applied_revision) rotation=original;
    if(face_viewer && placing && !holding) rotation=FaceWindowToHead(pose,head).orientation;
    editing=placing;
    pose.orientation=face_viewer ? rotation : pose.orientation;
    return pose;
  }
};
}
