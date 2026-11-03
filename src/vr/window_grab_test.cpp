#include "window_grab.h"
#include <limits>
#include <stdexcept>

namespace {
void Check(bool ok) { if(!ok) throw std::runtime_error("Window grab check failed"); }
bool Near(float a,float b) { return std::abs(a-b)<1e-4f; }
}
int main() {
  using namespace skate3::vr;
  XrPosef origin{{0,0,0,1},{0,0,0}};
  const auto window=WindowPose(origin,2,0);
  float distance=0,u=0,v=0;
  Check(HitWindow(origin,window,2.4f,16.f/9,distance,u,v) && Near(distance,2) && Near(u,0.5f) && Near(v,0.5f));
  auto aim=origin; aim.position.x=1.2f;
  Check(HitWindow(aim,window,2.4f,16.f/9,distance,u,v) && Near(u,1));
  aim.position.x=1.21f; Check(!HitWindow(aim,window,2.4f,16.f/9,distance,u,v));
  aim=origin; aim.position.z=-3; Check(!HitWindow(aim,window,2.4f,16.f/9,distance,u,v));
  aim=origin; aim.orientation={0,1,0,0}; Check(!HitWindow(aim,window,2.4f,16.f/9,distance,u,v));
  aim.orientation={0,0.7071068f,0,0.7071068f}; Check(!HitWindow(aim,window,2.4f,16.f/9,distance,u,v));
  aim=origin; aim.position.x=std::numeric_limits<float>::quiet_NaN();
  Check(!HitWindow(aim,window,2.4f,16.f/9,distance,u,v));
  WindowMotionFrame frame; frame.controls.connected=true; frame.hands.aim[1].valid=true;
  WindowGrab grab; EditorValues values;
  auto pointer=grab.Update(frame,values,0.01f); Check(pointer.hand==1 && !pointer.holding);
  frame.controls.grip[1]=1;
  pointer=grab.Update(frame,values,0.01f); Check(pointer.holding && Near(values.distance,2));
  frame.hands.aim[1].pose.position={0.3f,0.2f,-0.4f};
  pointer=grab.Update(frame,values,0.01f);
  Check(pointer.holding && Near(values.horizontal,0.3f) && Near(values.height,0.2f) && Near(values.distance,2.4f));
  frame.controls.stick[1][1]=1; frame.controls.stick[0][1]=1;
  const float width=values.width;
  grab.Update(frame,values,1);
  Check(values.distance<2.4f && Near(values.width,width+0.075f));
  frame.controls.stick[1][1]=frame.controls.stick[0][1]=0;
  frame.controls.grip[1]=0; pointer=grab.Update(frame,values,0.01f); Check(!pointer.holding);
  const auto released=values; frame.hands.aim[1].pose.position.x+=1;
  grab.Update(frame,values,0.01f); Check(Near(values.horizontal,released.horizontal));
  // Opening/reconnecting with a held grip must not acquire the window.
  values={}; frame={}; frame.controls.connected=true; frame.hands.aim[1].valid=true; frame.controls.grip[1]=1;
  grab.Reset(); Check(!grab.Update(frame,values,0.01f).holding);
  frame.controls.grip[1]=0; grab.Update(frame,values,0.01f);
  frame.controls.grip[1]=1; Check(grab.Update(frame,values,0.01f).holding);
  frame.hands.aim[1].valid=false; Check(!grab.Update(frame,values,0.01f).holding);
  frame.hands.aim[1].valid=true; Check(!grab.Update(frame,values,0.01f).holding);
  frame.controls.grip[1]=0; grab.Update(frame,values,0.01f);
  frame.controls.grip[1]=1; Check(grab.Update(frame,values,0.01f).holding);
  // The hand that presses wins over a closer idle pointer.
  grab.Reset(); values={}; frame={}; frame.controls.connected=true;
  frame.hands.aim[0].valid=frame.hands.aim[1].valid=true; frame.hands.aim[1].pose.position.z=-0.5f;
  grab.Update(frame,values,0.01f); frame.controls.grip[0]=1;
  Check(grab.Update(frame,values,0.01f).hand==0 && grab.holding());
  // Bounds, rotated LOCAL coordinates and off-centre anchors.
  grab.Reset(); values={}; frame={}; frame.controls.connected=true; frame.hands.aim[0].valid=true;
  frame.reference.orientation={0,0.7071068f,0,0.7071068f}; frame.hands.aim[0].pose.orientation=frame.reference.orientation;
  frame.hands.aim[0].pose.position=RotateVector(frame.reference.orientation,{0.6f,0,0});
  pointer=grab.Update(frame,values,0.01f); Check(pointer.hand==0 && Near(pointer.u,0.75f));
  frame.controls.grip[0]=1; grab.Update(frame,values,0.01f); Check(Near(values.horizontal,0));
  frame.hands.aim[0].pose.position=RotateVector(frame.reference.orientation,{50,50,-50});
  grab.Update(frame,values,0.01f); Check(values.horizontal==8 && values.height==2 && values.distance==8);
  // A missed press is consumed; pointing into the window while still holding cannot grab it.
  grab.Reset(); values={}; frame={}; frame.controls.connected=true; frame.hands.aim[0].valid=true;
  grab.Update(frame,values,0.01f); frame.hands.aim[0].pose.position.x=20; frame.controls.grip[0]=1;
  Check(!grab.Update(frame,values,0.01f).holding);
  frame.hands.aim[0].pose.position.x=0; Check(!grab.Update(frame,values,0.01f).holding);
  // Wrist rotation moves the grabbed point while keeping the window upright.
  grab.Reset(); values={}; frame={}; frame.controls.connected=true; frame.hands.aim[0].valid=true;
  grab.Update(frame,values,0.01f); frame.controls.grip[0]=1; grab.Update(frame,values,0.01f);
  frame.hands.aim[0].pose.orientation={0,std::sin(0.1f),0,std::cos(0.1f)};
  grab.Update(frame,values,0.01f);
  Check(Near(values.horizontal,-2*std::sin(0.2f)) && Near(values.distance,2*std::cos(0.2f)));
  frame.hands.aim[0].pose.position.x=std::numeric_limits<float>::infinity();
  Check(!grab.Update(frame,values,0.01f).holding);
  // The existing editor transaction still owns cancel/apply after motion updates.
  frame={}; frame.controls.connected=true; frame.hands.aim[0].valid=true; grab.Reset();
  EditorControls editor; editor.Begin(EditorMode::Window,{}); editor.Update({},0.01f);
  frame.controls.grip[0]=0; grab.Update(frame,editor.current,0.01f);
  frame.controls.grip[0]=1; grab.Update(frame,editor.current,0.01f);
  frame.hands.aim[0].pose.position.y=0.8f; grab.Update(frame,editor.current,0.01f);
  Check(Near(editor.current.height,0.8f));
  EditorInput cancel; cancel.cancel=true;
  Check(editor.Update(cancel,0.01f)==EditorResult::Cancel && editor.current.height==editor.original.height);
}
