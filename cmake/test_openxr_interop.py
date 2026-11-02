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
         "src/graphics/vulkan/native_rhi_vulkan.cpp"]


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

    def test_refuse_source_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            (sdk / ".git").write_text("gitdir: elsewhere")
            with self.assertRaises(ValueError):
                apply(sdk)

    def test_refuse_unrecognized_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            target = sdk / FILES[0]
            target.parent.mkdir(parents=True)
            target.write_text("Unrecognized Vulkan header")
            with self.assertRaises(ValueError):
                apply(sdk)


if __name__ == "__main__":
    unittest.main()
