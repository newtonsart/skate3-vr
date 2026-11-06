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
harness += section("// A row we add to the root", "std::atomic<bool> g_installed")
harness += "\nstd::vector<Page> g_pages;\nstd::vector<RootRow> g_root_rows;\n"
harness += section("int NearestIndex(", "bool HasPendingRestart()")
harness += section("Row* AddRow(", "// The overlay's restart-applied")
harness += section("void AddVrRows(", "Page BuildViewPage()")
harness += section("void AddMenuPage(", "// Categories each keep")
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
  for (int count : {0, 1, 8, 9, 25, 36}) {
    g_pages.clear();
    g_root_rows.clear();
    Page long_page{"View"};
    for (int i = 0; i < count; ++i) {
      Row r;
      r.title = std::to_string(i);
      r.get = [i] { return i; };
      r.pending = i;
      long_page.rows.push_back(std::move(r));
    }
    AddMenuPage(std::move(long_page));
    assert(g_root_rows.size() == (count ? 1 : 0));
    int visited = 0;
    for (int p = 0; p < static_cast<int>(g_pages.size()); ++p) {
      const Page& part = g_pages[p];
      assert(part.rows.size() <= 8 && !part.rows.empty());
      bool previous = false, next = false;
      for (const Row& r : part.rows) {
        if (r.page >= 0) {
          assert(r.page >= 0 && r.page < static_cast<int>(g_pages.size()));
          previous |= r.page == p - 1;
          next |= r.page == p + 1;
          assert(!ShownLabel(r));
        } else {
          assert(r.title == std::to_string(visited));
          assert(r.pending == visited && r.get() == visited);
          ++visited;
        }
      }
      assert(previous == (p > 0));
      assert(next == (p + 1 < static_cast<int>(g_pages.size())));
    }
    assert(visited == count);
    if (count > 8) {
      Row& next = g_pages.front().rows.back();
      assert(next.page == 1 && !StepRow(context, nullptr, 0, next, 1));
    }
  }
  // Pages appended after another category must navigate to absolute page indices.
  g_pages.clear(); g_root_rows.clear();
  AddMenuPage(Page{"Empty"});
  AddMenuPage(std::move(diagnostic));
  const int view_start = static_cast<int>(g_pages.size());
  AddMenuPage(std::move(page));
  assert(g_root_rows.size() == 2 && g_root_rows.back().page == view_start);
  assert(g_pages[view_start].rows.back().page == view_start + 1);
  puts("PASS: native VR rows, mode gating, actions and bounded pages with every setting retained");
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

    # Exercise the actual cap selection in builds with and without OpenXR.
    pacing = (ROOT / "src/skate3_native_render.cpp").read_text(encoding="utf-8")
    cap_selection = pacing[pacing.index("  double cap =", pacing.index("void PaceGuestFrame()")):
                           pacing.index("  static std::chrono::steady_clock::time_point s_next")]
    test.write_text(r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
double skate3_guest_fps_cap = 0;
bool skate3_guest_fps_cap_auto = true, skate3_vr = false;
#define REXCVAR_GET(name) name
namespace rex::ui {
struct Window {
  static inline float refresh = 75;
  static float CachedDisplayRefreshHz() { return refresh; }
  static float AutoFrameCapHz(float hz) {
    return hz < 30 ? 0 : std::floor(hz - std::max(4.0f, hz * 0.05f));
  }
};
}
double Cap() {
''' + cap_selection + r'''
  return cap;
}
int main() {
  assert(Cap() == 71);
  skate3_vr = true;
  assert(Cap() == (SKATE3_HAS_OPENXR ? 0 : 71));
  skate3_guest_fps_cap = 60;
  assert(Cap() == (SKATE3_HAS_OPENXR ? 0 : 71));
  skate3_guest_fps_cap_auto = false;
  assert(Cap() == 60);
  skate3_guest_fps_cap = 0;
  assert(Cap() == 0);
  skate3_vr = false; skate3_guest_fps_cap_auto = true;
  rex::ui::Window::refresh = 0; skate3_guest_fps_cap = 90;
  assert(Cap() == 90);
  puts("PASS: VR Auto bypasses the desktop cap; explicit limits and desktop fallback work");
}
''', encoding="utf-8")
    for openxr in (0, 1):
        subprocess.run([compiler, "-std=c++20", f"-DSKATE3_HAS_OPENXR={openxr}",
                        str(test), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
