import io
from pathlib import Path
import struct
import tempfile
import unittest

from ExtractGameIso import entries, extract


def fixture(name=b"default.xex"):
    image = bytearray(0x14000)
    image[0x10000:0x10014] = b"MICROSOFT*XBOX*MEDIA"
    struct.pack_into("<II", image, 0x10014, 34, 2048)
    def node(offset, filename, sector, length, flags=0, right=0):
        struct.pack_into("<HHIIBB", image, offset, 0, right, sector, length, flags, len(filename))
        image[offset + 14:offset + 14 + len(filename)] = filename
    node(34 * 2048, name, 38, 4, right=16)
    node(34 * 2048 + 64, b"data", 35, 2048, 0x10)
    node(35 * 2048, b"webkit", 36, 2048, 0x10)
    node(36 * 2048, b"EAWebkit.xex", 39, 4)
    image[38 * 2048:38 * 2048 + 4] = b"XEX2"
    image[39 * 2048:39 * 2048 + 4] = b"XEX2"
    return image


class IsoTests(unittest.TestCase):
    def test_extract_and_refuse_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "test.iso"
            source.write_bytes(fixture())
            extract(source, root / "game")
            self.assertEqual((root / "game/default.xex").read_bytes(), b"XEX2")
            self.assertEqual((root / "game/data/webkit/EAWebkit.xex").read_bytes(), b"XEX2")
            with self.assertRaises(ValueError):
                extract(source, root / "game")

    def test_reject_traversal(self):
        with self.assertRaises(ValueError):
            entries(io.BytesIO(fixture(b"../bad.xex")))

    def test_reject_cycle(self):
        image = fixture()
        struct.pack_into("<H", image, 34 * 2048 + 64, 16)
        with self.assertRaises(ValueError):
            entries(io.BytesIO(image))

    def test_reject_out_of_bounds(self):
        with self.assertRaises(ValueError):
            entries(io.BytesIO(fixture()[:0x11000]))


if __name__ == "__main__":
    unittest.main()
