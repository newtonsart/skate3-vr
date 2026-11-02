#include "stereo_math.h"
#include "cull_frustum.h"
#include "gpu_timing.h"
#include "openxr_passthrough.h"
#include "presentation.h"
#include <limits>
#include <cmath>
#include <stdexcept>

void Check(bool condition) { if(!condition) throw std::runtime_error("Stereo matrix check failed"); }
int main() {
  Check(skate3::vr::CanPresentWorld(true,1'000'000,false,false,true));
  Check(skate3::vr::CanPresentWorld(true,1'000'000,true,true,true));
  Check(!skate3::vr::CanPresentWorld(true,1'000'000,true,false,true));
  Check(!skate3::vr::CanPresentWorld(true,1'000'000,true,true,false));
  Check(!skate3::vr::CanPresentWorld(false,1'000'000,true,true,true));
  Check(!skate3::vr::CanPresentWorld(true,250'000'001,true,true,true));
  Check(!skate3::vr::CanPresentWorld(true,-1,true,true,true));
  // Unsupported passthrough must remain optional and avoid calling XR with
  // null handles, including repeated mode changes and shutdown.
  skate3::vr::OpenXrContext unavailable;
  skate3::vr::OpenXrPassthrough room(unavailable,XR_NULL_HANDLE);
  Check(!room.Enable() && !room.layer() && !room.error().empty());
  Check(!room.Enable() && !room.layer());
  room.Disable();
  Check(room.error().empty());
  Check(!room.Enable() && !room.layer());
  Check(std::abs(skate3::vr::GpuDurationMs(100,1100,64,1000)-1.0)<1e-9);
  Check(std::abs(skate3::vr::GpuDurationMs(250,4,8,100000)-1.0)<1e-9);
  Check(skate3::vr::GpuDurationMs(0,1,0,1)<0);
  Check(skate3::vr::GpuDurationMs(0,1,65,1)<0);
  Check(skate3::vr::GpuDurationMs(0,1,64,-1)<0);
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
  eye.pose.position={0,0,0}; eye.pose.orientation={0,0,0,1};
  skate3::vr::MakeEyeCamera(p,p,camera,reference,eye,2,vp,projection,position,3,1);
  Check(std::abs(position[0])<1e-5f && std::abs(position[1]-2)<1e-5f && std::abs(position[2]+6)<1e-5f);
  Check((10*vp[9]+vp[13])/(10*vp[11]+vp[15])<0);
  float baseline_vp[16],baseline_p[16],baseline_position[3];
  skate3::vr::MakeEyeCamera(p,p,camera,reference,eye,1,baseline_vp,baseline_p,baseline_position);
  skate3::vr::MakeEyeCamera(p,p,camera,reference,eye,1,vp,projection,position,0,0);
  for (int i=0;i<16;++i) Check(vp[i]==baseline_vp[i] && projection[i]==baseline_p[i]);
  bool bad_camera=false;
  try { skate3::vr::MakeEyeCamera(p,p,camera,reference,eye,1,vp,projection,position,
      std::numeric_limits<float>::quiet_NaN(),0); }
  catch (const std::runtime_error&) { bad_camera=true; }
  Check(bad_camera);
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
  // The physical window plane has zero stereo disparity. Near/far points
  // have opposite disparities; changing head orientation never rotates it.
  float window_vp[2][16],window_p[2][16],window_pos[2][3];
  eye.pose.orientation={0,0,0,1}; eye.pose.position={0,0,0};
  for (int e=0;e<2;++e) {
    eye.pose.position.x=e ? 0.032f : -0.032f;
    skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,1,2.4f,16.0f/9,2,0,
        window_vp[e],window_p[e],window_pos[e]);
    const float* m=window_vp[e];
    Check(std::abs((2*m[8]+m[12])/(2*m[11]+m[15]))<1e-5f);
    Check(std::abs((1.2f*window_p[e][0])/2-1)<1e-5f);
    // Original scene camera/projection remain untouched.
    Check(p[0]==-1 && camera[0]==0);
  }
  const auto disparity=[&](float z) {
    float x[2];
    for (int e=0;e<2;++e) { const float* m=window_vp[e]; x[e]=(z*m[8]+m[12])/(z*m[11]+m[15]); }
    return x[0]-x[1];
  };
  Check(disparity(1)>0 && disparity(4)<0);
  const float previous=window_vp[1][0];
  eye.pose.orientation={0,0.7071068f,0,0.7071068f};
  skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,1,2.4f,16.0f/9,2,0,
      vp,projection,position);
  Check(std::abs(vp[0]-previous)<1e-5f);
  // Head translation cancels in projection at the fixed window plane.
  eye.pose.position={0.3f,0.2f,0.4f};
  skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,1,2.4f,16.0f/9,2,0,
      vp,projection,position);
  Check(std::abs((2*vp[8]+vp[12])/(2*vp[11]+vp[15]))<1e-5f);
  Check(std::abs((2*vp[9]+vp[13])/(2*vp[11]+vp[15]))<1e-5f);
  const auto panel=skate3::vr::WindowPose(reference,2,0.3f);
  Check(std::abs(panel.position.z+2)<1e-5f && std::abs(panel.position.y-0.3f)<1e-5f);
  auto rotated_reference=reference;
  rotated_reference.orientation={0,0.7071068f,0,0.7071068f};
  const auto rotated_panel=skate3::vr::WindowPose(rotated_reference,2,0);
  Check(std::abs(rotated_panel.position.x+2)<1e-5f && std::abs(rotated_panel.position.z)<1e-5f);
  bool bad_window=false;
  try { skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,1,0,1,2,0,vp,projection,position); }
  catch (const std::runtime_error&) { bad_window=true; }
  Check(bad_window);
  // A pitched game camera and nonzero world position still converge at the
  // panel plane, with the head reference rotated independently in XR space.
  const float angle=0.4f,cs=std::cos(angle),sn=std::sin(angle);
  const float origin[3]={10,20,30};
  float view[16]={1,0,0,0, 0,cs,sn,0, 0,-sn,cs,0, -10,0,0,1};
  view[13]=-(origin[1]*cs-origin[2]*sn);
  view[14]=-(origin[1]*sn+origin[2]*cs);
  float pitched_vp[16]{};
  for (int r=0;r<4;++r) for (int c=0;c<4;++c) for (int k=0;k<4;++k)
    pitched_vp[r*4+c]+=view[r*4+k]*p[k*4+c];
  const float center[3]={10,20+2*sn,30+2*cs};
  rotated_reference.position={3,4,5};
  eye.pose.position={3.4f,4.2f,4.7f};
  skate3::vr::MakeWindowEyeCamera(pitched_vp,p,origin,rotated_reference,eye,1,
      2.4f,16.0f/9,2,0.1f,vp,projection,position);
  const auto clip=[&](int c) { return center[0]*vp[c]+center[1]*vp[4+c]+center[2]*vp[8+c]+vp[12+c]; };
  Check(clip(3)>0 && std::abs(clip(0)/clip(3))<1e-4f && std::abs(clip(1)/clip(3))<1e-4f);
  for (float invalid : {0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
    bool rejected_window=false;
    try { skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,invalid,2.4f,1,2,0,vp,projection,position); }
    catch (const std::runtime_error&) { rejected_window=true; }
    Check(rejected_window);
  }
}
