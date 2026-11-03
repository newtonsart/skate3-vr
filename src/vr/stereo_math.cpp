#include "stereo_math.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace skate3::vr {
namespace {
void Multiply(const float* a, const float* b, float* result) {
  for (int r=0;r<4;++r) for(int c=0;c<4;++c) {
    result[r*4+c]=0;
    for(int k=0;k<4;++k) result[r*4+c]+=a[r*4+k]*b[k*4+c];
  }
}
void Invert(const float* matrix, float* result) {
  double a[4][8]{};
  for(int r=0;r<4;++r) for(int c=0;c<4;++c) { a[r][c]=matrix[r*4+c]; a[r][c+4]=(r==c); }
  for(int c=0;c<4;++c) {
    int pivot=c;
    for(int r=c+1;r<4;++r) if(std::abs(a[r][c])>std::abs(a[pivot][c])) pivot=r;
    if(std::abs(a[pivot][c])<1e-10) throw std::runtime_error("Singular game projection");
    for(int k=0;k<8;++k) std::swap(a[c][k],a[pivot][k]);
    const double scale=a[c][c];
    for(int k=0;k<8;++k) a[c][k]/=scale;
    for(int r=0;r<4;++r) if(r!=c) {
      const double factor=a[r][c];
      for(int k=0;k<8;++k) a[r][k]-=factor*a[c][k];
    }
  }
  for(int r=0;r<4;++r) for(int c=0;c<4;++c) result[r*4+c]=float(a[r][c+4]);
}
XrQuaternionf Mul(XrQuaternionf a, XrQuaternionf b) {
  return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
          a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
          a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
          a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
}  // namespace
XrVector3f RotateVector(XrQuaternionf q, XrVector3f v) {
  auto t=Mul(Mul(q,{v.x,v.y,v.z,0}),{-q.x,-q.y,-q.z,q.w});
  return {t.x,t.y,t.z};
}
XrPosef RecenterPose(const XrPosef& head) {
  const auto& q=head.orientation;
  const float yaw=std::atan2(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x));
  return {{0,std::sin(yaw/2),0,std::cos(yaw/2)},head.position};
}
XrPosef WindowPose(const XrPosef& reference,float distance,float height,float horizontal) {
  if (!std::isfinite(distance) || distance<0.5f || !std::isfinite(height) || !std::isfinite(horizontal))
    throw std::runtime_error("Invalid window position");
  auto pose=reference;
  const auto offset=RotateVector(reference.orientation,{horizontal,height,-distance});
  pose.position.x+=offset.x; pose.position.y+=offset.y; pose.position.z+=offset.z;
  return pose;
}
void MakeWindowEyeCamera(const float* vp,const float* proj,const float* camera,
                         const XrPosef& reference,const XrView& eye,float scale,
                         float distance,
                         float* output,float* projection,float* position) {
  if (!std::isfinite(scale) || scale<=0 || !std::isfinite(distance) || distance<0.5f)
    throw std::runtime_error("Invalid stereo window");
  float inverse[16],game_view[16]; Invert(proj,inverse); Multiply(vp,inverse,game_view);
  const auto& q=reference.orientation;
  auto offset=RotateVector({-q.x,-q.y,-q.z,q.w},{eye.pose.position.x-reference.position.x,
      eye.pose.position.y-reference.position.y,eye.pose.position.z-reference.position.z});
  if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z))
    throw std::runtime_error("Invalid stereo window eye pose");
  // Preserve the game's full camera orientation. Head rotation changes how
  // the physical panel is viewed, without rotating the in-game camera.
  float axes[3][3];
  for (int a=0;a<3;++a) for (int k=0;k<3;++k)
    axes[a][k]=game_view[k*4+a]*(a==1 ? 1.0f : -1.0f);
  const float delta[3]={offset.x,offset.y,offset.z};
  for (int k=0;k<3;++k) {
    position[k]=camera[k];
    for (int a=0;a<3;++a) position[k]+=axes[a][k]*delta[a]*scale;
  }
  float view[16]{}; view[15]=1;
  for (int a=0;a<3;++a) {
    for (int k=0;k<3;++k) view[k*4+a]=axes[a][k];
    view[12+a]=-(position[0]*axes[a][0]+position[1]*axes[a][1]+position[2]*axes[a][2]);
  }
  const float depth=std::max(0.1f,distance+offset.z);
  // Fit the desktop view inside the panel; its physical size must not crop the game FOV.
  for (int r=0;r<4;++r) for (int c=0;c<4;++c)
    projection[r*4+c]=proj[r*4+c]*((r==0 || r==2) ? -1.0f : 1.0f);
  projection[0]*=depth/distance; projection[5]*=depth/distance;
  projection[8]-=projection[0]*offset.x/depth;
  projection[9]-=projection[5]*offset.y/depth;
  Multiply(view,projection,output);
}
void MakeHudTransform(const XrPosef& head,const XrView& eye,const float* projection,
                      float width,float aspect,float distance,float* transform) {
  if(!(width>0 && aspect>0 && distance>=0.5f)) throw std::runtime_error("Invalid HUD panel");
  const auto& q=head.orientation;
  const auto offset=RotateVector({-q.x,-q.y,-q.z,q.w},{eye.pose.position.x-head.position.x,
      eye.pose.position.y-head.position.y,eye.pose.position.z-head.position.z});
  const float depth=distance+offset.z;
  transform[0]=width*0.5f*projection[0]/depth;
  transform[1]=width*0.5f/aspect*projection[5]/depth;
  transform[2]=-projection[8]-offset.x*projection[0]/depth;
  transform[3]=-projection[9]-offset.y*projection[5]/depth;
}
void MakeEyeCamera(const float* vp,const float* proj,const float* camera,
                   const XrPosef& reference,const XrView& eye,float scale,
                   float* output,float* projection,float* position,float camera_distance,float camera_height) {
  if(!std::isfinite(scale) || scale<=0) throw std::runtime_error("Invalid VR world scale");
  if (!std::isfinite(camera_distance) || !std::isfinite(camera_height))
    throw std::runtime_error("Invalid VR camera offset");
  float inverse[16],view[16]; Invert(proj,inverse); Multiply(vp,inverse,view);
  // Skate's +Z-forward camera has negative projection X. OpenXR is -Z-forward.
  // Keep world +Y upright and remove the game's camera pitch/roll.
  float fx=view[2],fz=view[10];
  const float length=std::hypot(fx,fz);
  if(length<1e-5f) throw std::runtime_error("Game camera is vertical");
  fx/=length; fz/=length;
  const float basis[3][3]={{-fz,0,fx},{0,1,0},{-fx,0,-fz}};
  const XrQuaternionf invq{-reference.orientation.x,-reference.orientation.y,-reference.orientation.z,reference.orientation.w};
  const auto rotation=Mul(invq,eye.pose.orientation);
  const auto offset=RotateVector(invq,{eye.pose.position.x-reference.position.x,
      eye.pose.position.y-reference.position.y,eye.pose.position.z-reference.position.z});
  const float delta[3]={offset.x,offset.y+camera_height,offset.z+camera_distance};
  for(int k=0;k<3;++k) {
    position[k]=camera[k];
    for(int a=0;a<3;++a) position[k]+=delta[a]*basis[a][k]*scale;
  }
  float axes[3][3];
  for(int a=0;a<3;++a) {
    XrVector3f unit{}; if(a==0) unit.x=1; if(a==1) unit.y=1; if(a==2) unit.z=1;
    const auto v=RotateVector(rotation,unit); const float components[3]={v.x,v.y,v.z};
    for(int k=0;k<3;++k) { axes[a][k]=0; for(int b=0;b<3;++b) axes[a][k]+=components[b]*basis[b][k]; }
  }
  float eye_view[16]{}; eye_view[15]=1;
  for(int c=0;c<3;++c) {
    for(int r=0;r<3;++r) eye_view[r*4+c]=axes[c][r];
    eye_view[12+c]=-(position[0]*axes[c][0]+position[1]*axes[c][1]+position[2]*axes[c][2]);
  }
  const float l=std::tan(eye.fov.angleLeft),r=std::tan(eye.fov.angleRight);
  const float d=std::tan(eye.fov.angleDown),u=std::tan(eye.fov.angleUp);
  if(!(r>l && u>d)) throw std::runtime_error("Invalid OpenXR projection");
  std::fill(projection,projection+16,0.0f);
  const float near_z=0.05f*scale,far_z=10000*scale;
  projection[0]=2/(r-l); projection[5]=2/(u-d);
  projection[8]=(r+l)/(r-l); projection[9]=(u+d)/(u-d);
  projection[10]=-far_z/(far_z-near_z); projection[11]=-1;
  projection[14]=-far_z*near_z/(far_z-near_z);
  Multiply(eye_view,projection,output);
}
}
