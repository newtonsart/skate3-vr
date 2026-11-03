#pragma once
#include <filesystem>
struct ImGuiIO;
namespace rex::input { class InputSystem; }
namespace skate3::vr {
bool DrawVrEditor(ImGuiIO& io,rex::input::InputSystem* input,const std::filesystem::path& settings);
void CancelVrEditor();
int VrEditorPanelMode();
}
