#include "stereo_math.h"
#include "cull_frustum.h"
#include <limits>
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
  const float cull_camera[3]={10,20,30};
  const std::array<float,24> planes={1,0,1,-40, -1,0,1,-20,
      0,-1,1,-10, 0,1,1,-50, 0,0,1,-31, 0,0,-1,130};
  for (float convention : {1.0f,-1.0f}) for (float inside : {1.0f,-1.0f}) {
    auto original=planes;
    for (int p=0;p<6;++p) {
      for (int k=0;k<3;++k) original[p*4+k]*=inside;
      original[p*4+3]*=inside*convention;
    }
    std::array<float,24> expanded;
    Check(skate3::vr::ExpandMainCullFrustum(original,cull_camera,100,expanded));
    for (int p=0;p<6;++p) {
      for (int k=0;k<3;++k) Check(expanded[p*4+k]==original[p*4+k]);
      // Every signed half-space grows, so existing geometry cannot be rejected.
      Check(inside*convention*(expanded[p*4+3]-original[p*4+3])>0);
      const float behind=original[p*4]*10+original[p*4+1]*20+
          original[p*4+2]*-20+convention*expanded[p*4+3];
      Check(inside*behind>0);
    }
  }
  std::array<float,24> expanded;
  const float unrelated_camera[3]={1000,20,30};
  Check(!skate3::vr::ExpandMainCullFrustum(planes,unrelated_camera,100,expanded));
  auto invalid=planes; invalid[0]=std::numeric_limits<float>::quiet_NaN();
  Check(!skate3::vr::ExpandMainCullFrustum(invalid,cull_camera,100,expanded));
  Check(!skate3::vr::ExpandMainCullFrustum(planes,cull_camera,-1,expanded));
  const std::array<float,24> orthographic={1,0,0,-9, -1,0,0,11,
      0,-1,0,21, 0,1,0,-19, 0,0,1,-31, 0,0,-1,130};
  Check(!skate3::vr::ExpandMainCullFrustum(orthographic,cull_camera,100,expanded));
}
