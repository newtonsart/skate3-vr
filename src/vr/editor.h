#pragma once
#include <filesystem>
#include "window_grab.h"
struct ImGuiIO;
namespace rex::input { class InputSystem; }
namespace skate3::vr {
bool DrawVrEditor(ImGuiIO& io,rex::input::InputSystem* input,const std::filesystem::path& settings);
void CancelVrEditor();
int VrEditorPanelMode();
uint64_t WindowPlacementRevision();
void PublishWindowMotion(const WindowMotionFrame& frame);
WindowPointer ReadWindowPointer();
}
