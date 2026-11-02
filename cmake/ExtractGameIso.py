"""Extract a local XDVDFS game ISO without mounting it or changing the input."""
import argparse
from pathlib import Path
import struct


def entries(image):
    size = image.seek(0, 2)

    def read(offset, count):
        if offset < 0 or offset + count > size:
            raise ValueError("ISO extent outside input")
        image.seek(offset)
        data = image.read(count)
        if len(data) != count:
            raise ValueError("Truncated ISO")
        return data

    base = next((offset for offset in (0, 0xFB20, 0x20600, 0x2080000, 0xFD90000)
                 if offset + 0x10024 <= size and
                 read(offset + 0x10000, 20) == b"MICROSOFT*XBOX*MEDIA"), None)
    if base is None:
        raise ValueError("No recognized XDVDFS game partition")
    sector, length = struct.unpack("<II", read(base + 0x10014, 8))
    pending = [(base + sector * 2048, length, 0, Path())]
    seen = set()
    paths = set()
    result = []
    while pending:
        table, table_size, node, prefix = pending.pop()
        if (table, node) in seen or len(seen) >= 500000:
            raise ValueError("Cyclic or oversized directory tree")
        seen.add((table, node))
        if node + 14 > table_size:
            raise ValueError("Directory node outside table")
        left, right, sector, length, flags, name_size = struct.unpack(
            "<HHIIBB", read(table + node, 14))
        if not name_size or node + 14 + name_size > table_size:
            raise ValueError("Invalid directory name length")
        name = read(table + node + 14, name_size).decode("ascii")
        if name in (".", "..") or any(c in name for c in '/\\:<>"|?*\0') or name.endswith((".", " ")):
            raise ValueError("Unsafe ISO filename")
        path = prefix / name
        key = str(path).casefold()
        if key in paths:
            raise ValueError("Duplicate ISO path")
        paths.add(key)
        for child in (left, right):
            if child:
                pending.append((table, table_size, child * 4, prefix))
        offset = base + sector * 2048
        if offset + length > size:
            raise ValueError("File extent outside ISO")
        if flags & 0x10:
            if length:
                pending.append((offset, length, 0, path))
        else:
            result.append((path, offset, length))
    required = {"default.xex", "data/webkit/eawebkit.xex"}
    if not required.issubset(str(p).replace("\\", "/").lower() for p, _, _ in result):
        raise ValueError("ISO lacks required game executables")
    return result


def extract(source, destination):
    if destination.exists():
        raise ValueError("Destination must be new; refusing to overwrite files")
    with source.open("rb") as image:
        files = entries(image)
        destination.mkdir(parents=True)
        for path, offset, length in files:
            target = destination / path
            target.parent.mkdir(parents=True, exist_ok=True)
            image.seek(offset)
            with target.open("xb") as output:
                remaining = length
                while remaining:
                    data = image.read(min(remaining, 4 * 1024 * 1024))
                    if not data:
                        raise ValueError("Truncated ISO during extraction")
                    output.write(data)
                    remaining -= len(data)
        print(f"Extracted {len(files)} files ({sum(n for _, _, n in files)} bytes) to {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    extract(args.iso, args.destination)
