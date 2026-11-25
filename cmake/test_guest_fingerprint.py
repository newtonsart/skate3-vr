"""Test the optimized Windows fingerprint, including a guard-page fault."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


if sys.platform != "win32":
    print("Skipped: this test exercises the Windows SEH path")
    sys.exit(0)

root = Path(__file__).resolve().parents[1]
reader = (root / "src/native/skate3_native_guest_read.cpp").read_text(encoding="utf-8")
start = reader.index("namespace {\nbool FingerprintGuestSamples(")
end = reader.index("float GuestHalfToFloat(", start)
code = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <random>
#include <windows.h>

#define REX_RAW_ADDR(addr) (base + (addr))
""" + reader[start:end] + r"""

uint64_t Reference(const uint8_t* base, uint32_t v, uint32_t i, uint32_t vb,
                   uint32_t ic, uint64_t h) {
  for (uint32_t k = 0; k < 16; ++k) {
    const uint32_t addresses[] = {
        v + (uint32_t(uint64_t(vb - 8) * k / 15u) & ~7u),
        i + (uint32_t(uint64_t(ic * 2 - 8) * k / 15u) & ~7u)};
    for (uint32_t addr : addresses) {
      uint64_t value = 0;
      for (uint32_t j = 0; j < 8; ++j) value = value << 8 | base[addr + j];
      h = (h ^ value) * 1099511628211ull;
    }
  }
  return h;
}

int main() {
  auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x40000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  assert(base);
  std::mt19937 rng(7231935);
  for (int i = 0; i < 0x40000; ++i) base[i] = static_cast<uint8_t>(rng());
  for (int trial = 0; trial < 20000; ++trial) {
    const uint32_t v = 0x10000 + rng() % 0x8000, i = 0x20000 + rng() % 0x8000;
    const uint32_t vb = 8 + rng() % 0x1000, ic = 4 + rng() % 0x800;
    uint64_t hash = rng();
    const uint64_t expected = Reference(base, v, i, vb, ic, hash);
    assert(TryFingerprintGuestSamples(base, v, i, vb, ic, &hash));
    assert(hash == expected);
  }
  DWORD old_access = 0;
  assert(VirtualProtect(base + 0x31000, 0x1000, PAGE_NOACCESS, &old_access));
  uint64_t hash = 42;
  assert(!TryFingerprintGuestSamples(base, 0x30ff8, 0x20000, 128, 20, &hash) && hash == 42);
  assert(!TryFingerprintGuestSamples(base, 0x10000, 0x30ff8, 128, 20, &hash) && hash == 42);
  assert(!TryFingerprintGuestSamples(base, 0xffff, 0x20000, 128, 20, &hash) && hash == 42);
  assert(VirtualFree(base, 0, MEM_RELEASE));
  std::puts("PASS: 20000 fingerprints match and faults in either buffer preserve the input hash");
}
"""

compiler = shutil.which("clang++")
if compiler is None:
    raise RuntimeError("Run in a C++ developer shell with clang++ on PATH")
temp_root = root / "out/test-temp"
temp_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=temp_root) as directory:
    source = Path(directory) / "fingerprint.cpp"
    executable = source.with_suffix(".exe")
    source.write_text(code, encoding="utf-8")
    subprocess.run([compiler, "-std=c++23", "-O2", str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
