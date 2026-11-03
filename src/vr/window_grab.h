#pragma once
#include "editor_controls.h"
#include "openxr_gamepad.h"
#include "stereo_math.h"

namespace skate3::vr {
struct WindowMotionFrame {
  TouchControls controls;
  ControllerHands hands;
  XrPosef reference{{0,0,0,1},{0,0,0}};
  float aspect=16.f/9;
};
struct WindowPointer {
  int hand=-1;
  bool holding=false;
  float u=0.5f,v=0.5f;
};
inline XrVector3f Subtract(XrVector3f a,XrVector3f b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline XrVector3f InverseRotate(XrQuaternionf q,XrVector3f v) { return RotateVector({-q.x,-q.y,-q.z,q.w},v); }
inline bool ValidPointerPose(const XrPosef& pose) {
  const auto& p=pose.position; const auto& q=pose.orientation;
  const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
      std::abs(p.x)<1000 && std::abs(p.y)<1000 && std::abs(p.z)<1000 &&
      std::isfinite(norm) && std::abs(norm-1)<0.01f;
}
inline bool HitWindow(const XrPosef& aim,const XrPosef& window,float width,float aspect,
                      float& distance,float& u,float& v) {
  if(!ValidPointerPose(aim) || !ValidPointerPose(window) || !std::isfinite(width) ||
      !std::isfinite(aspect) || width<0.5f || width>6 || aspect<=0 || aspect>10) return false;
  const auto origin=InverseRotate(window.orientation,Subtract(aim.position,window.position));
  const auto direction=InverseRotate(window.orientation,RotateVector(aim.orientation,{0,0,-1}));
  if(origin.z<=0 || direction.z>=-0.001f) return false;
  distance=-origin.z/direction.z;
  if(!std::isfinite(distance) || distance<0.05f || distance>12) return false;
  u=(origin.x+direction.x*distance)/width+0.5f;
  v=0.5f-(origin.y+direction.y*distance)/(width/aspect);
  return u>=0 && u<=1 && v>=0 && v<=1;
}
class WindowGrab {
 public:
  void Reset() { hand_=-1; ready_[0]=ready_[1]=false; }
  bool holding() const { return hand_>=0; }
  WindowPointer Update(const WindowMotionFrame& frame,EditorValues& values,float dt) {
    WindowPointer pointer;
    if(!ValidPointerPose(frame.reference) || !(frame.aspect>0 && frame.aspect<=10)) { Reset(); return pointer; }
    const auto valid=[&](int hand) { return frame.controls.connected && frame.hands.aim[hand].valid &&
        ValidPointerPose(frame.hands.aim[hand].pose); };
    bool pressed[2]{};
    for(int hand=0;hand<2;++hand) {
      if(!valid(hand)) ready_[hand]=false;
      else if(ControllerAxis(frame.controls.grip[hand])<=0.35f) ready_[hand]=true;
      pressed[hand]=valid(hand) && ready_[hand] && frame.controls.grip[hand]>=0.65f;
      if(pressed[hand]) ready_[hand]=false;
    }
    if(hand_>=0 && (!valid(hand_) || ControllerAxis(frame.controls.grip[hand_])<=0.35f)) hand_=-1;
    const auto pose=WindowPose(frame.reference,values.distance,values.height,values.horizontal);
    float nearest=13;
    bool selected_pressed=false;
    for(int hand=1;hand>=0;--hand) {
      float distance=0,u=0,v=0;
      if(!valid(hand) || !HitWindow(frame.hands.aim[hand].pose,pose,values.width,frame.aspect,distance,u,v)) continue;
      if((pressed[hand] && !selected_pressed) || (pressed[hand]==selected_pressed && distance<nearest)) {
        nearest=distance; pointer={hand,false,u,v}; selected_pressed=pressed[hand];
      }
    }
    if(hand_<0 && pointer.hand>=0 && pressed[pointer.hand]) {
      hand_=pointer.hand; distance_=nearest; anchor_u_=pointer.u; anchor_v_=pointer.v; ready_[hand_]=false;
    }
    if(hand_>=0) {
      const float step=std::isfinite(dt) ? std::clamp(dt,0.f,0.05f) : 0.f;
      const auto stick=[&](int hand) { return EditorAxis(int16_t(ControllerAxis(frame.controls.stick[hand][1])*32767)); };
      distance_=std::clamp(distance_-stick(hand_)*step*1.5f,0.25f,12.f);
      values.width=std::clamp(values.width+stick(1-hand_)*step*1.5f,0.5f,6.f);
      const auto& aim=frame.hands.aim[hand_].pose;
      const auto direction=RotateVector(aim.orientation,{0,0,-1});
      const XrVector3f hit{aim.position.x+direction.x*distance_,aim.position.y+direction.y*distance_,aim.position.z+direction.z*distance_};
      const auto local=InverseRotate(frame.reference.orientation,Subtract(hit,frame.reference.position));
      values.horizontal=std::clamp(local.x-(anchor_u_-0.5f)*values.width,-8.f,8.f);
      values.height=std::clamp(local.y-(0.5f-anchor_v_)*values.width/frame.aspect,-2.f,2.f);
      values.distance=std::clamp(-local.z,0.5f,8.f);
      pointer={hand_,true,anchor_u_,anchor_v_};
    }
    return pointer;
  }
 private:
  int hand_=-1;
  bool ready_[2]{};
  float distance_=0,anchor_u_=0.5f,anchor_v_=0.5f;
};
}
