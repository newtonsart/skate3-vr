from pathlib import Path
import shutil
import tempfile
import unittest
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
         "src/ui/overlay/install_wizard_overlay.cpp", "thirdparty/CMakeLists.txt"]


class InteropTests(unittest.TestCase):
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
            self.assertIn('row.label = "VR View"', (sdk / FILES[8]).read_text())
            self.assertIn('"skate3_vr_window_feather"};', (sdk / FILES[8]).read_text())
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
