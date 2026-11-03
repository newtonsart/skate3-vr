#include "editor.h"
#include "editor_controls.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/input/input_system.h>
#include <rex/ui/overlay/simple_settings_overlay.h>
#include <imgui.h>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <mutex>

REXCVAR_DEFINE_INT32(skate3_vr_edit_mode,0,"VR","Live editor: 0 off, 1 window placement, 2 immersive camera").range(0,2);
REXCVAR_DEFINE_DOUBLE(skate3_vr_window_horizontal,0.0,"VR","Session-only horizontal window offset in meters").range(-8.0,8.0);

namespace skate3::vr {
namespace {
EditorControls editor;
int editor_view=0;
std::atomic<int> panel_mode{0};
WindowGrab window_grab;
std::mutex motion_mutex;
WindowMotionFrame motion_frame;
WindowPointer window_pointer;
std::chrono::steady_clock::time_point motion_time;
WindowMotionFrame ReadWindowMotion() {
  std::lock_guard lock(motion_mutex);
  return std::chrono::steady_clock::now()-motion_time<std::chrono::milliseconds(250) ? motion_frame : WindowMotionFrame{};
}
void SetWindowPointer(WindowPointer pointer) {
  std::lock_guard lock(motion_mutex); window_pointer=pointer;
}
EditorValues ReadValues() {
  return {float(rex::cvar::Query<double>("skate3_vr_window_horizontal")),
      float(rex::cvar::Query<double>("skate3_vr_window_height")),
      float(rex::cvar::Query<double>("skate3_vr_window_distance")),
      float(rex::cvar::Query<double>("skate3_vr_window_width")),
      float(rex::cvar::Query<double>("skate3_vr_camera_distance")),
      float(rex::cvar::Query<double>("skate3_vr_camera_height"))};
}
void WriteValues(const EditorValues& values) {
  const auto write=[](const char* name,float value) {
    if (float(rex::cvar::Query<double>(name))!=value)
      rex::cvar::SetFlagByName(name,std::to_string(value));
  };
  if (editor.mode==EditorMode::Window) {
    write("skate3_vr_window_horizontal",values.horizontal);
    write("skate3_vr_window_height",values.height);
    write("skate3_vr_window_distance",values.distance);
    write("skate3_vr_window_width",values.width);
  } else if (editor.mode==EditorMode::Camera) {
    write("skate3_vr_camera_distance",values.camera_distance);
    write("skate3_vr_camera_height",values.camera_height);
  }
}
}
int VrEditorPanelMode() { return panel_mode.load(); }
void PublishWindowMotion(const WindowMotionFrame& frame) {
  std::lock_guard lock(motion_mutex); motion_frame=frame; motion_time=std::chrono::steady_clock::now();
  if(!frame.controls.connected) window_pointer={};
}
WindowPointer ReadWindowPointer() {
  std::lock_guard lock(motion_mutex);
  return std::chrono::steady_clock::now()-motion_time<std::chrono::milliseconds(250) ? window_pointer : WindowPointer{};
}
void CancelVrEditor() {
  if (editor.mode!=EditorMode::None) WriteValues(editor.original);
  editor.mode=EditorMode::None;
  if (REXCVAR_GET(skate3_vr_edit_mode)!=0)
    rex::cvar::SetFlagByName("skate3_vr_edit_mode","0");
  panel_mode=0;
  window_grab.Reset(); SetWindowPointer({});
}
bool DrawVrEditor(ImGuiIO& io,rex::input::InputSystem* input,const std::filesystem::path& settings) {
  const int requested=REXCVAR_GET(skate3_vr_edit_mode);
  const int view=rex::cvar::Query<int32_t>("skate3_vr_mode");
  if (!requested || (requested==1)!=(view!=0) || (editor.mode!=EditorMode::None && editor_view!=view)) {
    CancelVrEditor(); return false;
  }
  const auto mode=static_cast<EditorMode>(requested);
  if (editor.mode!=mode) { CancelVrEditor(); editor.Begin(mode,ReadValues());
    editor_view=view;
    rex::cvar::SetFlagByName("skate3_vr_edit_mode",std::to_string(requested));
    REXLOG_INFO("VR editor: started {}",requested==1 ? "window placement" : "immersive camera"); }
  panel_mode=requested;
  rex::input::X_INPUT_GAMEPAD pad{};
  if (input) input->GetUiGamepadState(&pad);
  EditorInput controls;
  controls.horizontal=EditorAxis(pad.thumb_lx);
  controls.vertical=EditorAxis(pad.thumb_ly);
  controls.depth=EditorAxis(pad.thumb_ry);
  controls.resize=float(bool(pad.buttons&rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER))-
      float(bool(pad.buttons&rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER));
  controls.apply=(pad.buttons&rex::input::X_INPUT_GAMEPAD_A) || ImGui::IsKeyDown(ImGuiKey_Enter);
  controls.cancel=(pad.buttons&rex::input::X_INPUT_GAMEPAD_B) || ImGui::IsKeyDown(ImGuiKey_Escape);
  controls.horizontal+=float(ImGui::IsKeyDown(ImGuiKey_D))-float(ImGui::IsKeyDown(ImGuiKey_A));
  controls.vertical+=float(ImGui::IsKeyDown(ImGuiKey_W))-float(ImGui::IsKeyDown(ImGuiKey_S));
  controls.depth+=float(ImGui::IsKeyDown(ImGuiKey_UpArrow))-float(ImGui::IsKeyDown(ImGuiKey_DownArrow));
  controls.resize+=float(ImGui::IsKeyDown(ImGuiKey_Equal))-float(ImGui::IsKeyDown(ImGuiKey_Minus));
  WindowPointer pointer;
  if(mode==EditorMode::Window && editor.armed && !controls.apply && !controls.cancel) {
    pointer=window_grab.Update(ReadWindowMotion(),editor.current,io.DeltaTime);
    if(pointer.holding) controls.horizontal=controls.vertical=controls.depth=0;
  }
  SetWindowPointer(pointer);
  const auto result=editor.Update(controls,io.DeltaTime);
  WriteValues(editor.current);

  const auto region=EditorPanelRegion(uint32_t(io.DisplaySize.x),uint32_t(io.DisplaySize.y));
  const float x=float(region.x),y=float(region.y);
  const float scale=std::min(io.DisplaySize.x/1920.f,io.DisplaySize.y/1080.f);
  auto* draw=ImGui::GetForegroundDrawList();
  draw->AddRectFilled({x,y},{x+region.width,y+region.height},IM_COL32(15,20,25,255),12*scale);
  const auto text=[&](float row,float size,ImU32 color,const char* message) {
    draw->AddText(ImGui::GetFont(),size*scale,{x+20*scale,y+row*scale},color,message);
  };
  const bool placing=mode==EditorMode::Window;
  text(12,34,IM_COL32(255,255,255,255),placing ? "Move window" : "Adjust immersive camera");
  char values[192];
  const auto& v=editor.current;
  if (placing) std::snprintf(values,sizeof(values),"Horizontal %.2f m   Height %.2f m   Distance %.2f m   Width %.2f m",v.horizontal,v.height,v.distance,v.width);
  else std::snprintf(values,sizeof(values),"Camera distance %+.2f m   Camera height %+.2f m",v.camera_distance,v.camera_height);
  text(65,28,IM_COL32(170,220,245,255),values);
  text(108,26,IM_COL32(230,230,230,255),placing ? "Touch: point + hold grip to move; release to anchor. Held stick: distance; other stick: size."
      : "Left stick up / down: height   Right stick up / down: distance");
  text(151,26,IM_COL32(230,230,230,255),placing ? "A: Place window   B: Cancel" : "A: Apply camera   B: Cancel");
  text(190,22,IM_COL32(185,190,195,255),placing ? "Gamepad: left stick move, right stick distance, LB/RB size. Keyboard: WASD / arrows / - +."
      : "Keyboard: WASD / arrows / - +   Enter: Apply   Escape: Cancel");
  if (result!=EditorResult::Editing) {
    if (result==EditorResult::Apply) rex::ui::SaveSimpleSettingsConfig(settings);
    REXLOG_INFO("VR editor: {}",result==EditorResult::Apply ? "applied" : "cancelled and restored");
    editor.mode=EditorMode::None;
    rex::cvar::SetFlagByName("skate3_vr_edit_mode","0");
    window_grab.Reset(); SetWindowPointer({});
  }
  return true;
}
}
