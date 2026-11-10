from pathlib import Path
import shutil
import tempfile
import unittest
import subprocess
from ApplyOpenXRInterop import apply

SOURCE = Path(__file__).resolve().parents[1] / "third_party/rexglue-sdk"
FILES = ["include/rex/ui/vulkan/instance.h", "src/ui/vulkan/vulkan_instance.cpp",
         "src/ui/vulkan/vulkan_provider.cpp", "src/ui/vulkan/vulkan_device.cpp",
         "include/rex/graphics/vulkan/command_processor.h",
         "include/rex/graphics/vulkan/native_rhi_vulkan.h",
         "src/graphics/vulkan/native_rhi_vulkan.cpp",
         "include/rex/ui/overlay/simple_settings_overlay.h",
         "src/ui/overlay/simple_settings_overlay.cpp",
         "include/rex/ui/imgui_drawer.h", "src/ui/imgui_drawer.cpp",
         "include/rex/ui/vulkan/presenter.h", "src/ui/vulkan/vulkan_presenter.cpp",
         "src/graphics/vulkan/command_processor.cpp",
         "include/rex/ui/overlay/install_wizard_overlay.h",
         "src/ui/overlay/install_wizard_overlay.cpp", "thirdparty/CMakeLists.txt",
         "src/ui/rex_app.cpp", "src/core/cvar.cpp", "src/audio/xma_context.cpp",
         "include/rex/ui/vulkan/mem_alloc.h", "src/ui/vulkan/vulkan_mem_alloc.cpp"]


class InteropTests(unittest.TestCase):
    def test_startup_failure_reports_cause_and_stops_setup(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory) / "sdk"
            for name in FILES:
                target = sdk / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(SOURCE / name, target)
            apply(sdk)
            app = (sdk / "src/ui/rex_app.cpp").read_text()
            start = app.index("  try {\n    OnPreSetup(config_);")
            guard = app[start:app.index("  // A consumer may substitute", start)]
            code = r'''
#include <cassert>
#include <stdexcept>
#include <string>
std::string logged,shown;
namespace fmt {
std::string format(const char*,const char* cause) {
  return std::string("Application initialization failed: ")+cause;
}
}
#define REXLOG_ERROR(pattern,message) (logged=message)
namespace rex {
enum class SimpleMessageBoxType { Error };
void ShowSimpleMessageBox(SimpleMessageBoxType type,const std::string& msg) {
  assert(type==SimpleMessageBoxType::Error); shown=msg;
}
}
struct App {
  int config_=0,hook_calls=0,graphics_setups=0;
  bool fail=false;
  void OnPreSetup(int&) {
    ++hook_calls;
    if(fail) throw std::runtime_error("Detect HMD (connect Quest Link and activate PCVR) failed: XrResult=-35");
  }
  bool Setup() {
'''+guard+r'''
    ++graphics_setups;
    return true;
  }
};
int main() {
  App app;
  assert(app.Setup() && app.hook_calls==1 && app.graphics_setups==1);
  assert(logged.empty() && shown.empty());
  app={}; app.fail=true;
  assert(!app.Setup() && app.hook_calls==1 && app.graphics_setups==0);
  assert(shown==logged && shown.find("connect Quest Link")!=std::string::npos);
  assert(shown.find("XrResult=-35")!=std::string::npos);
}
'''
            cpp = Path(directory) / "startup.cpp"
            exe = Path(directory) / "startup.exe"
            cpp.write_text(code)
            compiler = shutil.which("clang++") or shutil.which("g++")
            self.assertIsNotNone(compiler, "Run in a C++ developer shell")
            subprocess.run([compiler, "-std=c++20", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_external_submission_wait_and_failures(self):
        patch = (Path(__file__).parent / "ApplyOpenXRInterop.py").read_text()
        start = patch.index("  bool FlushNativeForExternal(bool wait)")
        method = patch[start:patch.index("\n\n  // Returns", start)]
        source = r'''
#include <cassert>
#include <vector>
struct Processor {
  std::vector<int> calls;
  bool end_ok=true, wait_ok=true, begin_ok=true;
  bool EndSubmission(bool swap) { assert(!swap); calls.push_back(1); return end_ok; }
  bool AwaitAllQueueOperationsCompletion() { calls.push_back(2); return wait_ok; }
  bool BeginSubmission(bool guest) { assert(!guest); calls.push_back(3); return begin_ok; }
'''+method+r'''
};
int main() {
  Processor p; assert(p.FlushNativeForExternal(true));
  assert((p.calls==std::vector<int>{1,2,3}));
  p={}; assert(p.FlushNativeForExternal(false));
  assert((p.calls==std::vector<int>{1,3}));
  for (bool wait : {false,true}) {
    p={}; p.end_ok=false; assert(!p.FlushNativeForExternal(wait));
    assert((p.calls==std::vector<int>{1}));
    p={}; p.begin_ok=false; assert(!p.FlushNativeForExternal(wait));
  }
  p={}; p.wait_ok=false; assert(!p.FlushNativeForExternal(true));
  assert((p.calls==std::vector<int>{1,2}));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "submission.cpp"
            exe = Path(directory) / "submission.exe"
            cpp.write_text(source)
            compiler = shutil.which("clang++") or shutil.which("g++")
            self.assertIsNotNone(compiler, "Run in a C++ developer shell")
            subprocess.run([compiler, "-std=c++20", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_pinned_patch_and_repeat(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            for name in FILES:
                target = sdk / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(SOURCE / name, target)
            apply(sdk)
            first = [(sdk / name).read_bytes() for name in FILES]
            apply(sdk)
            self.assertEqual(first, [(sdk / name).read_bytes() for name in FILES])
            self.assertIn("FlushNativeForExternal", (sdk / FILES[4]).read_text())
            self.assertIn("hooks.create_instance", (sdk / FILES[1]).read_text())
            self.assertIn("user_dir = rex::to_path(user_data_cvar)", (sdk / "src/ui/rex_app.cpp").read_text())
            self.assertIn("leaving it unchanged", (sdk / "src/core/cvar.cpp").read_text())
            self.assertIn('row.label = "VR View"', (sdk / FILES[8]).read_text())
            self.assertIn('"Auto (OpenXR)"', (sdk / FILES[8]).read_text())
            self.assertIn('In VR, Auto uses OpenXR pacing.', (sdk / FILES[8]).read_text())
            self.assertIn('"skate3_vr_window_feather",', (sdk / FILES[8]).read_text())
            overlay = (sdk / "src/ui/overlay/simple_settings_overlay.cpp").read_text()
            vr = overlay[overlay.index("case 5: {  // VR"):overlay.index("case 0: {  // Video")]
            self.assertIn('row.label = "VR View"', vr)
            self.assertIn('row.label = "VRAM Block Size"', vr)
            self.assertIn('"vulkan_native_vma_block_mb"};', overlay)
            self.assertIn('edges.label = "VR Window Edges"', (sdk / FILES[8]).read_text())
            self.assertIn('stereo.label = "VR Window Depth"', (sdk / FILES[8]).read_text())
            self.assertIn('"Face Viewer While Placing"', (sdk / FILES[8]).read_text())
            self.assertIn('"VR Editor Comfort View"', (sdk / FILES[8]).read_text())
            self.assertIn('recenter.label = "Recenter VR"', (sdk / FILES[8]).read_text())
            self.assertIn('"Adjust camera" : "Move window"', (sdk / FILES[8]).read_text())
            self.assertIn('live_editor_(io)', (sdk / FILES[8]).read_text())
            self.assertIn('SetLiveEditor', (sdk / FILES[7]).read_text())
            self.assertIn("mirror_end_();", (sdk / FILES[10]).read_text())
            self.assertIn("command_buffers[3]", (sdk / FILES[12]).read_text())
            self.assertIn("NativeRhiApplyOutputSizeOverride(guest_output_width", (sdk / FILES[13]).read_text())

    def test_refuse_source_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            (sdk / ".git").write_text("gitdir: elsewhere")
            with self.assertRaises(ValueError):
                apply(sdk)

    def test_refuse_unrecognized_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            for name in FILES:
                copied = sdk / name
                copied.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(SOURCE / name, copied)
            target = sdk / FILES[0]
            target.write_text("Unrecognized Vulkan header")
            with self.assertRaises(ValueError):
                apply(sdk)


if __name__ == "__main__":
    unittest.main()
