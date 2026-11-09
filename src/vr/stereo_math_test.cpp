#include "stereo_math.h"
#include "simulated_route.h"
#include "cull_frustum.h"
#include "gpu_timing.h"
#include "perf_trace.h"
#include "openxr_passthrough.h"
#include "presentation.h"
#include "render_resolution.h"
#include "editor_controls.h"
#include "window_blend.h"
#include "window_facing.h"
#include "hud_backdrop.h"
#include "hud_clip.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <stdexcept>
#include <vector>

void Check(bool condition) { if(!condition) throw std::runtime_error("Stereo matrix check failed"); }
int main() {
  {
    skate3::vr::PerfTrace trace;
    float vp[16]{}, proj[16]{};
    proj[11]=1; vp[11]=1;
    trace.Add(0,11,2,3,4,-1,1,vp,proj);
    vp[3]=1; vp[11]=0;
    trace.Add(22,11,5,7,9,4,1,vp,proj,{.1,8,.2,.7,.3,.8});
    Check(trace.worst_xr.wait==8 && trace.worst_xr.tracking==.7 && trace.worst_xr.end==.3);
    Check(trace.frames==2 && trace.intervals==1 && trace.slow==1 && trace.repeated==1);
    Check(trace.interval_max==22 && trace.render_max==5 && trace.wait_max==7 && trace.xr_wait_max==9 && trace.gpu_max==4 && trace.gpu_samples==1);
    Check(trace.worst_scene==1 && trace.worst_render==5 && trace.worst_wait==7 && trace.worst_xr_wait==9 && trace.worst_gpu==4 && std::abs(trace.worst_yaw_delta-3.14159265358979323846/2)<1e-6);
    Check(std::abs(trace.yaw_travel-3.14159265358979323846/2)<1e-6);
    skate3::vr::FrameTransfers transfer;
    transfer.eyes[0].fence=6;
    transfer.overlay.total=4;
    trace.Add(21,11,4,5,2,1,3,vp,proj,{.1,1,.2,.3,.3,4,5,2,true},2600,{},transfer);
    Check(trace.worst_scene==1 && trace.worst_transfer_scene==3 && trace.worst_transfer_items==2600);
    Check(trace.transfer_max==9 && trace.worst_transfer.eyes[0].fence==6);
    Check(trace.prefetched_frames==1 && trace.exposed_wait_sum==1 && trace.wait_call_sum==5 && trace.wait_ready_age_max==2);
    trace.ResetWindow();
    Check(trace.worst_xr.wait==0 && trace.worst_xr.end==0);
    Check(trace.transfer_max==0 && trace.prefetched_frames==0 && trace.worst_transfer_scene==0);
    trace.Add(11,11,1,1,1,0,2,vp,proj);
    Check(trace.frames==1 && trace.slow==0 && trace.repeated==0 && trace.yaw_travel==0);
  }
  using namespace skate3::vr;
  {
    Check(!SimulatedRouteControls(-1).connected && !SimulatedRouteControls(90).connected);
    Check(!SimulatedRouteControls(std::numeric_limits<double>::quiet_NaN()).connected);
    Check(SimulatedRouteControls(0).a && !SimulatedRouteControls(0.3).a);
    Check(SimulatedRouteControls(13).stick[0][0]>0 && SimulatedRouteControls(33).stick[0][0]<0);
    Check(SimulatedRouteControls(25).stick[0][0]==0 && SimulatedRouteControls(89).connected);
    for (int i=0;i<90;++i) {
      const auto c=SimulatedRouteControls(i);
      Check(!c.menu && !c.b && !c.y && !c.x && std::abs(c.stick[0][0])<=1);
    }
  }
  {
    const auto normal=FpsPanelCrop(1920,1080,1920,1080,{1710,10,200,130});
    Check(normal.x==1710 && normal.y==10 && normal.width==200 && normal.height==130);
    Check(std::abs(normal.aspect-200.f/130)<1e-6);
    const auto scaled=FpsPanelCrop(1280,720,1920,1080,{1070,10,200,130});
    Check(scaled.x==1605 && scaled.y==15 && scaled.width==300 && scaled.height==195);
    Check(scaled.aspect==normal.aspect);
    const auto small=FpsPanelCrop(240,160,1920,1080,{200,140,200,130});
    Check(small.x==1600 && small.y==945 && small.width==320 && small.height==135 && small.aspect==2);
    Check(FpsPanelCrop(0,720,1920,1080,{0,0,200,130}).width==0);
    Check(FpsPanelCrop(1280,720,1920,1080,{}).width==0);
    Check(FpsPanelCrop(1280,720,1920,1080,{1280,0,10,10}).width==0);
    for (uint32_t width : {1u,319u,320u,1280u,3840u}) {
      for (uint32_t height : {1u,199u,200u,720u,2160u}) {
        const auto crop=FpsPanelCrop(width,height,1920,1080,{width/2,height/2,200,130});
        Check(crop.width>0 && crop.height>0 && crop.x+crop.width<=1920 && crop.y+crop.height<=1080);
      }
    }
  }
  {
  const float full_hud[4]={1,1,0,0};
  auto clip=HudPanelClip(1920,1080,full_hud);
  Check(clip.left==0 && clip.top==0 && clip.right==1920 && clip.bottom==1080);
  const float small_hud[4]={0.5f,0.25f,0.125f,-0.125f};
  clip=HudPanelClip(1920,1080,small_hud);
  Check(clip.left==600 && clip.top==472 && clip.right==1560 && clip.bottom==742);
  // A widget parked below the desktop frame stays outside the reduced HUD.
  const float parked_y=-1.2f*small_hud[1]+small_hud[3];
  Check((1-parked_y)*540>=clip.bottom);
  clip=HudPanelClip(1920,1080,full_hud,0.5f);
  Check(clip.left==480 && clip.right==1440 && clip.top==0 && clip.bottom==1080);
  const float mirrored_hud[4]={-0.5f,-0.25f,0.125f,-0.125f};
  auto mirrored_clip=HudPanelClip(1920,1080,mirrored_hud);
  clip=HudPanelClip(1920,1080,small_hud);
  Check(clip.left==mirrored_clip.left && clip.right==mirrored_clip.right && clip.top==mirrored_clip.top && clip.bottom==mirrored_clip.bottom);
  const float outside_hud[4]={0.25f,0.25f,3,3};
  clip=HudPanelClip(1920,1080,outside_hud);
  Check(clip.left==clip.right && clip.top==clip.bottom);
  float invalid_hud[4]={1,1,0,std::numeric_limits<float>::quiet_NaN()};
  clip=HudPanelClip(1920,1080,invalid_hud); Check(clip.left==clip.right && clip.top==clip.bottom);
  clip=HudPanelClip(0,1080,full_hud); Check(clip.left==clip.right);
  for(float offset : {-0.7f,-0.03f,0.03f,0.7f}) {
    const float eye_hud[4]={0.37f,0.23f,offset,-offset};
    clip=HudPanelClip(43,31,eye_hud);
    for(int y=0;y<31;++y) for(int x=0;x<43;++x) {
      const double nx=(x+0.5)*2/43-1,ny=1-(y+0.5)*2/31;
      const bool inside=nx>=double(offset)-eye_hud[0] && nx<double(offset)+eye_hud[0] &&
          ny<=-double(offset)+eye_hud[1] && ny>-double(offset)-eye_hud[1];
      Check(inside==(x>=clip.left && x<clip.right && y>=clip.top && y<clip.bottom));
    }
  }
  }
  Check(SelectEditorView(true,true,true)==EditorView::Photo);
  Check(SelectEditorView(true,false,true)==EditorView::Objects);
  Check(SelectEditorView(true,false,false)==EditorView::Default);
  Check(SelectEditorView(false,true,true)==EditorView::Default);
  Check(WindowFeather(0,true,0.08)==0 && WindowFeather(1,true,0.08)==0);
  Check(WindowFeather(2,false,0.08)==0 && WindowFeather(2,true,0)==0);
  Check(std::abs(WindowFeather(2,true,0.08)-0.08f)<1e-6f);
  for(double invalid : {-0.1,0.16,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
    bool rejected=false;
    try { WindowFeather(2,true,invalid); } catch(const std::runtime_error&) { rejected=true; }
    Check(rejected);
  }
  Check(EditorAxis(0)==0 && EditorAxis(4000)==0 && EditorAxis(-4000)==0);
  Check(EditorAxis(32767)==1 && EditorAxis(-32768)==-1);
  EditorControls editor;
  EditorValues saved;
  editor.Begin(EditorMode::Window,saved);
  EditorInput input;
  input.apply=true; input.vertical=1;
  Check(editor.Update(input,1)==EditorResult::Editing && !editor.armed);
  input={}; editor.Update(input,0.01f);
  Check(editor.armed);
  input.horizontal=1; input.vertical=1; input.depth=1; input.resize=1;
  editor.Update(input,0.05f);
  Check(editor.current.horizontal>0 && editor.current.height>0 &&
      editor.current.distance<saved.distance && editor.current.width>saved.width);
  const auto moved=editor.current;
  editor.Update(input,std::numeric_limits<float>::quiet_NaN());
  Check(editor.current.distance==moved.distance);
  input.cancel=true;
  Check(editor.Update(input,0.01f)==EditorResult::Cancel);
  Check(editor.current.horizontal==saved.horizontal && editor.current.height==saved.height &&
      editor.current.distance==saved.distance && editor.current.width==saved.width);
  editor.Begin(EditorMode::Window,saved); editor.Update({},0.01f);
  input={1,1,1,1};
  for (int i=0;i<1000;++i) editor.Update(input,1);
  Check(editor.current.horizontal==8 && editor.current.height==2 && editor.current.distance==0.5f && editor.current.width==6);
  input={-1,-1,-1,-1};
  for (int i=0;i<1000;++i) editor.Update(input,1);
  Check(editor.current.horizontal==-8 && editor.current.height==-2 && editor.current.distance==8 && editor.current.width==0.5f);
  editor.Begin(EditorMode::Camera,saved); editor.Update({},0.01f);
  input={1,1,1,1}; editor.Update(input,0.05f);
  Check(editor.current.camera_height>0 && editor.current.camera_distance<0 && editor.current.width==saved.width && editor.current.horizontal==0);
  input={}; input.apply=true;
  Check(editor.Update(input,0.01f)==EditorResult::Apply && editor.current.camera_height>0);
  input={0,1,1,0};
  for (int i=0;i<1000;++i) editor.Update(input,0.05f);
  Check(editor.current.camera_height==4 && editor.current.camera_distance==-2);
  input={0,-1,-1,0};
  for (int i=0;i<1000;++i) editor.Update(input,0.05f);
  Check(editor.current.camera_height==-2 && editor.current.camera_distance==8);
  input={}; input.cancel=true;
  Check(editor.Update(input,0.01f)==EditorResult::Cancel && editor.current.camera_height==0 && editor.current.camera_distance==0);
  editor.Begin(EditorMode::Window,saved); editor.Update({},0.01f);
  input={0,0,1,0}; editor.Update(input,10);
  Check(std::abs(editor.current.distance-1.95f)<1e-5f);
  for (auto extent : {RenderExtent{1,1},RenderExtent{1920,1080},RenderExtent{2560,1440},RenderExtent{2272,2464}}) {
    const auto crop=EditorPanelRegion(extent.width,extent.height);
    Check(crop.width>0 && crop.height>0 && crop.x+crop.width<=extent.width && crop.y+crop.height<=extent.height);
  }
  const auto legacy=skate3::vr::EyeRenderExtent(2272,2464,0);
  Check(!legacy.width && !legacy.height);
  const auto native=skate3::vr::EyeRenderExtent(2272,2464,1);
  Check(native.width==2272 && native.height==2464);
  const auto reduced=skate3::vr::EyeRenderExtent(2272,2464,0.75);
  Check(reduced.width==1704 && reduced.height==1848);
  const auto supersampled=skate3::vr::EyeRenderExtent(2272,2464,1.25);
  Check(supersampled.width==2840 && supersampled.height==3080);
  Check(std::abs(skate3::vr::PanelAspect(1280,720,2272,2464)-16.f/9)<1e-6f);
  Check(std::abs(skate3::vr::PanelAspect(0,0,1920,1080)-16.f/9)<1e-6f);
  for (double invalid : {-1.,1.5,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
    bool rejected=false;
    try { skate3::vr::EyeRenderExtent(2272,2464,invalid); }
    catch (const std::runtime_error&) { rejected=true; }
    Check(rejected);
  }
  for (auto dimensions : {skate3::vr::RenderExtent{0,2464},skate3::vr::RenderExtent{16384,2464}}) {
    bool rejected=false;
    try { skate3::vr::EyeRenderExtent(dimensions.width,dimensions.height,1); }
    catch (const std::runtime_error&) { rejected=true; }
    Check(rejected);
  }
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
  Check(std::abs(skate3::vr::GpuTimestampOffsetMs(250,4,8,100000)-1.0)<1e-9);
  Check(std::abs(skate3::vr::GpuTimestampOffsetMs(4,250,8,100000)+1.0)<1e-9);
  Check(std::abs(skate3::vr::GpuTimestampOffsetMs(1100,100,64,1000)+1.0)<1e-9);
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
    skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,1,4,
        window_vp[e],window_p[e],window_pos[e]);
    const float* m=window_vp[e];
    Check(std::abs((2*m[8]+m[12])/(2*m[11]+m[15]))<1e-5f);
    Check(window_p[e][0]==-p[0] && window_p[e][5]==p[5]);
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
  skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,1,4,
      vp,projection,position);
  Check(std::abs(vp[0]-previous)<1e-5f);
  // Head translation cancels in projection at the fixed window plane.
  eye.pose.position={0.3f,0.2f,0.4f};
  skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,1,4,
      vp,projection,position);
  Check(std::abs((2*vp[8]+vp[12])/(2*vp[11]+vp[15]))<1e-5f);
  Check(std::abs((2*vp[9]+vp[13])/(2*vp[11]+vp[15]))<1e-5f);
  const auto panel=skate3::vr::WindowPose(reference,2,0.3f);
  Check(std::abs(panel.position.z+2)<1e-5f && std::abs(panel.position.y-0.3f)<1e-5f);
  auto rotated_reference=reference;
  rotated_reference.orientation={0,0.7071068f,0,0.7071068f};
  const auto rotated_panel=skate3::vr::WindowPose(rotated_reference,2,0);
  Check(std::abs(rotated_panel.position.x+2)<1e-5f && std::abs(rotated_panel.position.z)<1e-5f);
  const auto moved_panel=skate3::vr::WindowPose(rotated_reference,2,0.3f,1);
  Check(std::abs(moved_panel.position.x+2)<1e-5f && std::abs(moved_panel.position.z+1)<1e-5f && std::abs(moved_panel.position.y-0.3f)<1e-5f);
  bool bad_horizontal=false;
  try { skate3::vr::WindowPose(reference,2,0,std::numeric_limits<float>::infinity()); }
  catch (const std::runtime_error&) { bad_horizontal=true; }
  Check(bad_horizontal);
  bool bad_window=false;
  try { skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,1,0,vp,projection,position); }
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
      4,vp,projection,position);
  const auto clip=[&](int c) { return center[0]*vp[c]+center[1]*vp[4+c]+center[2]*vp[8+c]+vp[12+c]; };
  Check(clip(3)>0 && std::abs(clip(0)/clip(3))<1e-4f && std::abs(clip(1)/clip(3))<1e-4f);
  // With the head centered, the entire desktop frustum (including its edges,
  // lens shift and clipping) must survive every window distance and world scale.
  eye.pose.position=rotated_reference.position;
  for (float fov : {45.f,60.f,90.f,110.f}) {
    float desktop_p[16];
    std::copy(p,p+16,desktop_p);
    desktop_p[5]=1/std::tan(fov*3.14159265f/360);
    desktop_p[0]=-desktop_p[5]/(16.f/9);
    desktop_p[8]=0.12f; desktop_p[9]=-0.07f;
    float desktop_vp[16]{};
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) for (int k=0;k<4;++k)
      desktop_vp[r*4+c]+=view[r*4+k]*desktop_p[k*4+c];
    for (float distance : {0.5f,2.f,8.f}) for (float scale : {0.5f,1.f,2.f}) {
      skate3::vr::MakeWindowEyeCamera(desktop_vp,desktop_p,origin,rotated_reference,eye,
          scale,distance,vp,projection,position);
      for (int i=0;i<16;++i) Check(std::abs(vp[i]-desktop_vp[i])<1e-4f);
      for (int i=0;i<3;++i) Check(std::abs(position[i]-origin[i])<1e-5f);
    }
  }
  for (float invalid : {0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
    bool rejected_window=false;
    try { skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,invalid,2,vp,projection,position); }
    catch (const std::runtime_error&) { rejected_window=true; }
    Check(rejected_window);
    rejected_window=false;
    try { skate3::vr::MakeWindowEyeCamera(p,p,camera,reference,eye,1,invalid,vp,projection,position); }
    catch (const std::runtime_error&) { rejected_window=true; }
    Check(rejected_window);
  }
  // Far-point image disparity is one physical IPD for any screen width/FOV/scale.
  reference={{0,0,0,1},{0,0,0}};
  for(float width : {0.5f,2.4f,6.f}) for(float scale : {0.5f,1.f,2.f}) for(float focal : {0.6f,1.f,2.f}) {
    float game_p[16]; std::copy(p,p+16,game_p); game_p[0]=-focal;
    float far_x[2];
    for(int e=0;e<2;++e) {
      eye.pose.position={e ? 0.032f : -0.032f,0,0};
      MakeWindowEyeCamera(game_p,game_p,camera,reference,eye,scale,width,vp,projection,position);
      far_x[e]=(1e6f*vp[8]+vp[12])/(1e6f*vp[11]+vp[15]);
      const float convergence=focal*width*scale*0.5f;
      Check(std::abs((convergence*vp[8]+vp[12])/(convergence*vp[11]+vp[15]))<1e-5f);
    }
    Check(std::abs((far_x[0]-far_x[1])*width*0.5f+0.064f)<1e-5f);
  }
  eye.pose.position={0.032f,0,0};
  for(float aspect : {1.f,16.f/9,2.4f}) {
    float far_y[2];
    for(int e=0;e<2;++e) {
      eye.pose.position={0,e ? 0.032f : -0.032f,0};
      MakeWindowEyeCamera(p,p,camera,reference,eye,1,2.4f,vp,projection,position,1,aspect);
      far_y[e]=(1e6f*vp[9]+vp[13])/(1e6f*vp[11]+vp[15]);
    }
    Check(std::abs(std::abs((far_y[0]-far_y[1])*2.4f/aspect*0.5f)-0.064f)<1e-5f);
  }
  eye.pose.position={0.032f,0,0};
  MakeWindowEyeCamera(p,p,camera,reference,eye,1,2.4f,vp,projection,position,0);
  for(int k=0;k<16;++k) Check(std::abs(vp[k]-p[k])<1e-5f);
  const auto fixed=WindowPose(reference,2,0.4f,1);
  WindowFacing anchor;
  const auto orientation_equal=[](XrQuaternionf a,XrQuaternionf b) {
    return std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)+std::abs(a.w-b.w)<1e-5f;
  };
  const auto placed=anchor.Update(fixed,{0,0,0},true,false,false,0);
  Check(orientation_equal(placed.orientation,anchor.Update(fixed,{3,2,1},true,false,false,0).orientation));
  const auto editing=anchor.Update(fixed,{3,2,1},true,true,false,0);
  Check(!orientation_equal(editing.orientation,placed.orientation));
  Check(orientation_equal(editing.orientation,anchor.Update(fixed,{-2,-1,0},true,true,true,0).orientation));
  Check(orientation_equal(placed.orientation,anchor.Update(fixed,{-2,-1,0},true,false,false,0).orientation));
  const auto applied=anchor.Update(fixed,{3,2,1},true,true,false,0);
  Check(orientation_equal(applied.orientation,anchor.Update(fixed,{-2,-1,0},true,false,false,1).orientation));
  Check(orientation_equal(applied.orientation,anchor.Update(fixed,{0,0,0},true,false,false,1).orientation));
  Check(orientation_equal(fixed.orientation,anchor.Update(fixed,{3,2,1},false,false,false,1).orientation));
  anchor={};
  Check(orientation_equal(FaceWindowToHead(fixed,{3,2,1}).orientation,
      anchor.Update(fixed,{3,2,1},true,false,false,1).orientation));
  for(XrVector3f head : {XrVector3f{0,0,0},XrVector3f{3,2,1},XrVector3f{-2,-1,0}}) {
    const auto facing=FaceWindowToHead(fixed,head);
    Check(facing.position.x==fixed.position.x && facing.position.y==fixed.position.y && facing.position.z==fixed.position.z);
    const auto normal=RotateVector(facing.orientation,{0,0,1});
    const XrVector3f delta{head.x-fixed.position.x,head.y-fixed.position.y,head.z-fixed.position.z}; const float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
    Check(std::abs(normal.x-delta.x/length)<1e-5f && std::abs(normal.y-delta.y/length)<1e-5f && std::abs(normal.z-delta.z/length)<1e-5f);
  }
  float constants[36]{}; constants[0]=constants[5]=constants[10]=constants[15]=1;
  constants[16]=constants[21]=constants[26]=constants[31]=1;
  std::fill(constants+32,constants+36,1.f);
  std::array<uint8_t,4*28> vertices{};
  const float positions[4][4]={{-1,-1,0,1},{1,-1,0,1},{-1,1,0,1},{1,1,0,1}};
  for(int i=0;i<4;++i) { std::memcpy(vertices.data()+i*28,positions[i],16); std::fill(vertices.begin()+i*28+24,vertices.begin()+i*28+28,255); }
  constants[32]=constants[33]=constants[34]=0;
  const auto fill=DescribeHudQuad(0x29,16,0,constants,vertices,4,28);
  Check(fill.valid && fill.black && fill.left==-1 && fill.right==1);
  Check(FullscreenHudBackdrop(fill));
  Check(!DescribeHudQuad(0x29,16,0,constants,vertices,5,28).valid);
  std::vector<HudQuad> loading{fill};
  HudQuad tile; tile.flags=0x0d; tile.source_stride=24; tile.width=tile.height=64;
  tile.valid=true; tile.left=0.8f; tile.right=0.9f; tile.bottom=-0.9f; tile.top=-0.8f;
  Check(!FullscreenHudBackdrop(tile));
  for(int i=0;i<5;++i) loading.push_back(tile);
  Check(LoadingBackdrop(loading)==0);
  for(float alpha : {0.f,0.05f,0.35f,0.8f,1.f}) {
    constants[35]=alpha;
    vertices[27]=uint8_t(alpha*255);
    loading[0]=DescribeHudQuad(0x29,16,0,constants,vertices,4,28);
    Check(LoadingBackdrop(loading)==0 && FullscreenHudBackdrop(loading[0]));
  }
  loading[0]=fill;
  Check(LoadingBackdrop(std::span<const HudQuad>(&fill,1))==-1);
  loading.back().width=512; Check(LoadingBackdrop(loading)==-1); loading.back()=tile;
  loading.back().top=0; Check(LoadingBackdrop(loading)==-1); loading.back()=tile;
  loading[0].black=false; Check(LoadingBackdrop(loading)==-1); loading[0]=fill;
  loading[0].right=0; Check(LoadingBackdrop(loading)==-1); loading[0]=fill;
  HudQuad font; loading.push_back(font); Check(LoadingBackdrop(loading)==-1);
}
