#include "stereo_math.h"
#include <cmath>
#include <stdexcept>

void Check(bool condition) { if(!condition) throw std::runtime_error("Stereo matrix check failed"); }
int main() {
  const float p[16]={-1,0,0,0,0,1,0,0,0,0,1.001f,1,0,0,-0.1001f,0};
  float camera[3]={0,0,0},vp[16],projection[16],position[3];
  XrPosef reference{{0,0,0,1},{0,0,0}};
  XrView eye{XR_TYPE_VIEW}; eye.pose.orientation.w=1;
  eye.fov={-0.7853982f,0.7853982f,0.7853982f,-0.7853982f};
  eye.pose.position.x=-0.032f;
  skate3::vr::MakeEyeCamera(p,p,camera,reference,eye,1,vp,projection,position);
  Check(std::abs(position[0]-0.032f)<1e-5f);
  float clipx=10*vp[8]+vp[12],clipw=10*vp[11]+vp[15];
  Check(clipw>0 && clipx/clipw>0);
  eye.pose.position.x=0.032f;
  skate3::vr::MakeEyeCamera(p,p,camera,reference,eye,1,vp,projection,position);
  Check(std::abs(position[0]+0.032f)<1e-5f);
  Check((10*vp[8]+vp[12])/(10*vp[11]+vp[15])<0);
  eye.pose.position.x=0; eye.fov.angleRight=1.0f;
  skate3::vr::MakeEyeCamera(p,p,camera,reference,eye,1,vp,projection,position);
  Check(std::abs(projection[8])>0.05f);
  const float right=std::tan(eye.fov.angleRight);
  Check(std::abs((right*projection[0]-projection[8])-1)<1e-5f);
  eye.pose.orientation={0,std::sqrt(0.5f),0,std::sqrt(0.5f)};
  eye.fov.angleRight=0.7853982f;
  skate3::vr::MakeEyeCamera(p,p,camera,reference,eye,1,vp,projection,position);
  Check(std::abs(10*vp[0]+vp[12])<1e-4f);
  Check(10*vp[3]+vp[15]>0);
  bool rejected=false; try { skate3::vr::MakeEyeCamera(p,p,camera,reference,eye,0,vp,projection,position); }
  catch(const std::runtime_error&) { rejected=true; } Check(rejected);
  eye.pose.orientation={0,0,0,1}; eye.pose.position.x=-0.032f;
  float hud[4]; skate3::vr::MakeHudTransform(reference,eye,projection,1.6f,16.0f/9.0f,2.0f,hud);
  Check(std::abs(hud[0]-0.4f)<1e-5f && hud[2]>0);
  eye.pose.position.x=0.032f;
  skate3::vr::MakeHudTransform(reference,eye,projection,1.6f,16.0f/9.0f,2.0f,hud);
  Check(hud[2]<0 && hud[1]<hud[0]);
}
