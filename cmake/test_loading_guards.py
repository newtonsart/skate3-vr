"""Regression checks for speculative mesh pointers and old-XMA buffer ends.

Run with Python and clang++ in a C++ developer shell; no game or headset needed.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from ApplyOpenXRInterop import apply
from test_openxr_interop import FILES, SOURCE

ROOT = Path(__file__).resolve().parents[1]


def between(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise RuntimeError("Run from a C++ developer shell with clang++ or g++ on PATH")

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    sdk = work / "sdk"
    for name in FILES:
        target = sdk / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(SOURCE / name, target)
    apply(sdk)
    audio = (sdk / "src/audio/xma_context.cpp").read_text(encoding="utf-8")
    guest = (ROOT / "src/native/skate3_native_guest_read.cpp").read_text(encoding="utf-8")
    gpu = (ROOT / "src/skate3_native_scene_gpu.cpp").read_text(encoding="utf-8")
    readable = between(guest, "bool GuestMetadataReadable(", "// Lock-free guarded bulk copy")
    metadata = between(gpu, "  uint8_t record[0x60];", "  if (!record_ok) {")
    scene = (ROOT / "src/skate3_native_scene.cpp").read_text(encoding="utf-8")
    payload_guard = between(scene, "  if (!GuestMetadataReadable(item.vb_addr", "    g_rej_geom.fetch_add")
    start = audio.index("    BitStream stream(current_input_buffer, current_input_size * 8);")
    guard = audio[start:audio.index("    if (old_packets_skip_ > 0)", start)]
    finish = between(audio, "    // Finish a block before storing", "    if (offset <= data->input_buffer_read_offset)")
    next_offset = between(audio, "              const uint32_t next_offset =", "            } else {\n              old_is_stream_done_ = true;")

    harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
namespace rex::memory { enum class PageAccess { kNoAccess, kReadOnly, kReadWrite }; }
struct Heap {
  struct Range { uint32_t first, last; };
  std::vector<Range> ranges;
  rex::memory::PageAccess QueryRangeAccess(uint32_t lo, uint32_t hi) {
    for (auto r : ranges) if (lo >= r.first && hi <= r.last)
      return rex::memory::PageAccess::kReadOnly;
    return rex::memory::PageAccess::kNoAccess;
  }
};
struct Memory {
  Heap heap;
  Heap* LookupHeap(uint32_t) { return &heap; }
} memory;
namespace rex::system {
Memory* current = &::memory;
Memory* kernel_memory() { return current; }
}
'''
    harness += readable
    harness += r'''
uint8_t fixture[0x60]{};
struct DrawItem { uint32_t vb_addr=0, vb_bytes=0, ib_addr=0, ib_count=0; };
struct Entry { uint32_t mesh; };
bool GuestTryCopy(void* dst, const void* src, size_t size) {
  assert(reinterpret_cast<uintptr_t>(src) == 0x40010000 && size == sizeof(fixture));
  std::memcpy(dst, fixture, size); return true;
}
bool Metadata(const Entry& e) {
  uint8_t* base = nullptr;
'''
    harness += metadata + "  return record_ok;\n}\n"
    harness += r'''
int fingerprint_calls=0;
bool ComputeItemFingerprint(uint8_t*, DrawItem&) { ++fingerprint_calls; return true; }
bool Payload(DrawItem item) {
  uint8_t* base=nullptr;
'''
    harness += payload_guard + "    return false;\n  }\n  return true;\n}\n"
    harness += r'''
void Pointer(uint32_t off, uint32_t value) {
  value = __builtin_bswap32(value); std::memcpy(fixture + off, &value, 4);
}
constexpr uint32_t kBitsPerPacket = 16384;
struct Data {
  uint32_t input_buffer_read_offset = 100, output_buffer_valid = 1, packets = 1;
  bool loop = false;
  uint32_t GetCurrentInputBufferPacketCount() { return packets; }
};
int errors = 0, swaps = 0, seeks = 0;
#define REXAPU_ERROR(...) (++errors)
int id() { return 1; }
void SwapInputBuffer(Data* data) { ++swaps; data->input_buffer_read_offset = 32; }
bool TrySetupNextLoopOld(Data* data, bool) {
  if (data->loop) { data->input_buffer_read_offset = 128; return true; }
  return false;
}
struct BitStream {
  size_t bits;
  BitStream(uint8_t*, size_t n) : bits(n) {}
  void SetOffset(size_t offset) { assert(offset < bits); ++seeks; }
};
struct Ring {
  bool full;
  int read_offset() { return 0; }
  int write_offset() { return full ? 0 : 1; }
};
void Seek(Data* data) {
  uint8_t* current_input_buffer = nullptr;
  const size_t current_input_size = 2048;
'''
    harness += guard + "}\n"
    harness += r'''
bool old_is_stream_done_ = false;
void Finish(Data* data, uint32_t offset, bool is_streaming, bool full = false) {
  bool reuse_input_buffer = false;
  const size_t current_input_size = 2048;
  Ring output_rb{full};
'''
    harness += finish + "  data->input_buffer_read_offset = offset;\n}\n"
    harness += r'''
uint32_t packet_offset = 0;
uint32_t GetPacketFirstFrameOffsetOld(Data*) { return packet_offset; }
void NextOffset(Data* data) {
'''
    harness += next_offset + "}\n"
    harness += r'''
int main() {
  assert(!GuestMetadataReadable(0, 4));
  assert(!GuestMetadataReadable(0x40010000, 0));
  assert(!GuestMetadataReadable(0xfffffff0, 32));
  memory.heap.ranges = {{0x40010000, 0x4001005f}, {0x40020000, 0x4002003f},
                        {0x40030000, 0x40030023}, {0x40040000, 0x40040023}};
  assert(GuestMetadataReadable(0x40010000, 0x60));
  assert(!GuestMetadataReadable(0x40010000, 0x61));
  rex::system::current = nullptr;
  assert(!GuestMetadataReadable(0x40010000, 4));
  rex::system::current = &memory;
  Pointer(0x28, 0x40020000); Pointer(0x30, 0x40030000); Pointer(0x34, 0x40040000);
  assert(Metadata({0x40010000}));
  for (uint32_t bad : {0x7441726du, 0x3666b7aau, 0x00050006u,
                        0x0507c648u, 0x36d75088u, 0x616e6365u}) {
    for (uint32_t field : {0x28u, 0x30u, 0x34u}) {
      uint32_t saved; std::memcpy(&saved, fixture + field, 4);
      Pointer(field, bad);
      assert(!Metadata({0x40010000}));
      std::memcpy(fixture + field, &saved, 4);
      assert(Metadata({0x40010000}));
    }
  }
  assert(!Metadata({0x50000000}));
  DrawItem item{0x40020000, 0x40, 0x40030000, 0x12};
  assert(Payload(item));
  assert(fingerprint_calls==1);
  auto bad=item; bad.vb_addr=0x0507c648; assert(!Payload(bad));
  bad=item; bad.ib_addr=0x007a8598; assert(!Payload(bad));
  bad=item; bad.vb_bytes=0x41; assert(!Payload(bad));
  bad=item; bad.ib_count=0x13; assert(!Payload(bad));
  bad=item; bad.ib_count=0x80000012; assert(!Payload(bad));
  assert(fingerprint_calls==1);
  Data data;
  Seek(&data); assert(seeks == 1 && swaps == 0 && errors == 0);
  data.input_buffer_read_offset = 16384;
  Seek(&data); assert(seeks == 1 && swaps == 1 && errors == 0);
  data.input_buffer_read_offset = 16416;
  Seek(&data); assert(seeks == 1 && swaps == 2 && errors == 1);
  for (uint32_t end : {16384u, 16416u}) {
    data = {}; old_is_stream_done_ = false;
    Finish(&data, end, false); assert(old_is_stream_done_);
    data = {}; old_is_stream_done_ = false;
    Finish(&data, end, true, true);
    assert(!old_is_stream_done_ && data.input_buffer_read_offset == 32 && !data.output_buffer_valid);
    data = {}; data.loop = true; old_is_stream_done_ = false;
    Finish(&data, end, true);
    assert(!old_is_stream_done_ && data.input_buffer_read_offset == 128);
  }
  data = {}; Finish(&data, 1000, false); assert(data.input_buffer_read_offset == 1000);
  data.input_buffer_read_offset = 32; packet_offset = 16416;
  NextOffset(&data); assert(data.input_buffer_read_offset == 32);
  packet_offset = 400; NextOffset(&data); assert(data.input_buffer_read_offset == 400);
  puts("PASS: speculative mesh pointers, XMA ends/loops, invalid offsets and next-buffer bounds");
}
'''
    test = work / "loading.cpp"
    binary = work / ("loading.exe" if os.name == "nt" else "loading")
    test.write_text(harness, encoding="utf-8")
    subprocess.run([compiler, "-std=c++20", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
