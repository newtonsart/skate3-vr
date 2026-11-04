"""Exercise the native VR row builder without loading the game or an OpenXR runtime.

Run from a C++ developer shell: python cmake/test_vr_options_menu.py
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/skate3_options_menu.cpp").read_text(encoding="utf-8")


def section(start, end):
    return source[source.index(start):source.index(end)]


declarations = "\n".join(
    f'flags["{name}"] = "{value}";'
    for name, value in re.findall(
        r"REXCVAR_DEFINE_(?:BOOL|INT32|DOUBLE)\((skate3_vr\w*),\s*([^,]+),",
        "\n".join((ROOT / f"src/vr/{name}.cpp").read_text(encoding="utf-8")
                  for name in ("game_vr", "editor")),
    )
)
harness = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
std::unordered_map<std::string, std::string> flags;
namespace rex::cvar {
template<class T> T Query(std::string_view name) {
  auto text = flags.at(std::string(name));
  if constexpr (std::is_same_v<T, std::string>) return text;
  else if constexpr (std::is_same_v<T, bool>) return text == "true";
  else return static_cast<T>(std::stod(text));
}
bool SetFlagByName(std::string_view name, std::string_view value) {
  flags.at(std::string(name)) = value;
  return true;
}
}
bool HasCvar(std::string_view name) { return flags.contains(std::string(name)); }
std::function<void()> g_open_settings;
'''
harness += section("struct Row {", "// A row we add to the root")
harness += section("int NearestIndex(", "bool HasPendingRestart()")
harness += section("Row* AddRow(", "// The overlay's restart-applied")
harness += section("void AddVrRows(", "Page BuildViewPage()")
harness += r'''
struct PPCContext {};
int saved = 0;
std::function<void()> g_save_settings = [] { ++saved; };
constexpr int kSelectorLeftSound = 0, kSelectorRightSound = 1;
constexpr int kRefreshScreen = 1, kRefreshValues = 50;
void PlaySound(PPCContext&, uint8_t*, int) {}
void RefreshScreen(PPCContext&, uint8_t*, uint32_t, int) {}
'''
harness += section("int SteppedIndex(", "void CopyGuestString(")
harness += '\nint main() {\n' + declarations + r'''
  Page desktop{"View"};
  AddVrRows(desktop);
  assert(desktop.rows.empty());
  flags["skate3_vr_settings_test"] = "true";
  Page diagnostic{"View"};
  AddVrRows(diagnostic);
  assert(diagnostic.rows.size() == 20);
  flags["skate3_vr_settings_test"] = "false";
  flags["skate3_vr"] = "true";
  Page page{"View"};
  AddVrRows(page);
  assert(page.rows.size() == 20);
  auto row = [&](const char* title) -> Row& {
    auto it = std::find_if(page.rows.begin(), page.rows.end(),
                           [&](const Row& r) { return r.title == title; });
    assert(it != page.rows.end());
    return *it;
  };
  auto enabled = [](const Row& r) { return !r.enabled || r.enabled(); };
  for (const Row& r : page.rows) {
    if (r.action) assert(!ShownLabel(r));
    else assert(r.get && r.set && ShownLabel(r).has_value());
  }
  assert(!enabled(row("Move window")) && enabled(row("Adjust camera")));
  row("VR View").set(1);
  assert(enabled(row("Move window")) && !enabled(row("Adjust camera")));
  assert(!enabled(row("VR Window Edges")));
  row("VR View").set(2);
  assert(enabled(row("VR Window Edges")));
  assert(!enabled(row("VR HUD Width")) && !enabled(row("VR Loading Symbol Only")));
  auto& camera = row("VR Camera Distance");
  camera.set(0);
  assert(rex::cvar::Query<double>("skate3_vr_camera_distance") == -2);
  camera.set(static_cast<int>(camera.labels.size()) - 1);
  assert(rex::cvar::Query<double>("skate3_vr_camera_distance") == 8);
  row("VR Render Resolution").set(3);
  assert(rex::cvar::Query<double>("skate3_vr_render_scale") == 1.25);
  row("VR Window Depth").set(1);
  assert(rex::cvar::Query<double>("skate3_vr_window_depth") == 0.5);
  int opened = 0;
  g_open_settings = [&] { ++opened; };
  row("Move window").action();
  assert(opened == 1 && rex::cvar::Query<int>("skate3_vr_edit_mode") == 1);
  row("Adjust camera").action();
  assert(opened == 2 && rex::cvar::Query<int>("skate3_vr_edit_mode") == 2);
  row("Recenter VR").action();
  row("Capture VR frame").action();
  assert(rex::cvar::Query<bool>("skate3_vr_recenter_requested"));
  assert(rex::cvar::Query<bool>("skate3_vr_capture_once"));
  PPCContext context;
  assert(!StepRow(context, nullptr, 0, row("Move window"), 1));
  assert(saved == 0);
  assert(StepRow(context, nullptr, 0, row("VR Render Resolution"), 1));
  assert(saved == 1 && rex::cvar::Query<double>("skate3_vr_render_scale") == 0);
  assert(StepRow(context, nullptr, 0, row("VR Render Resolution"), -1));
  assert(saved == 2 && rex::cvar::Query<double>("skate3_vr_render_scale") == 1.25);
  puts("PASS: native VR rows, ranges, mode gating and editor actions");
}
'''
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise RuntimeError("Put clang++ or g++ on PATH (Windows: use a developer shell)")
with tempfile.TemporaryDirectory() as directory:
    test = Path(directory) / "vr_options.cpp"
    binary = Path(directory) / ("vr_options.exe" if os.name == "nt" else "vr_options")
    test.write_text(harness, encoding="utf-8")
    subprocess.run([compiler, "-std=c++20", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
