"""Exercise the renderer's actual route refresh and guarded word reader."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/skate3_native_scene_gpu.cpp').read_text()
header = (root / 'src/skate3_native_scene_gpu_internal.h').read_text()
reader = source[source.index('bool ReadStableTexWords('):source.index('// Frames-per-second estimate')]
route = source[source.index('      auto rit = g_r.tex_routes.find(tex_ptr);'):source.index('      auto sit = g_r.tex_store.find(route.key);')]
struct = header[header.index('  struct TexRoute {'):header.index('  std::unordered_map<uint32_t, TexRoute> tex_routes;')]
prepared = source[source.index('      const bool prepare = prepare_item'):source.index('#endif\n      tex_pending = false;',source.index('      const bool prepare = prepare_item'))]
sticky = header[header.index('  struct TexStickySite {'):header.index('  std::unordered_map<uint64_t, TexStickySite> tex_sticky;')]
code = r'''
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
uint8_t memory[128]{};
int reads=0;
bool readable=true, changing=false;
bool GuestTryCopy(void* dst,const void* src,size_t n) {
  ++reads;
  if (!readable) return false;
  std::memcpy(dst,src,n);
  if (changing) static_cast<uint8_t*>(dst)[0] ^= reads&1;
  return true;
}
uint32_t SwapU32(uint32_t v) { return __builtin_bswap32(v); }
// The unchanged decoder mapping is a boundary here: 18 is DXT1, 32 unsupported.
namespace xenos { enum class TextureFormat : uint32_t {}; }
struct HostTextureFormat {};
bool GetHostTextureFormat(xenos::TextureFormat f,HostTextureFormat&) {
  return uint32_t(f)==18;
}
'''+reader+r'''
struct GuestTexture {};
struct RendererState {
'''+struct+r'''
  std::unordered_map<uint32_t,TexRoute> tex_routes;
'''+sticky+r'''
  std::unordered_map<uint64_t,TexStickySite> tex_sticky;
  uint64_t texture_revision=0;
  GuestTexture white;
} g_r;
uint64_t FetchWordsKey(const uint32_t words[6]) {
  uint64_t key=0; for(int i=0;i<6;++i) key=key*31+words[i]; return key;
}
std::atomic<uint64_t> g_demote_hold{0};
#define REXLOG_INFO(...)
uint64_t frame_number=0;
bool retained=false,tex_pending=false;
const GuestTexture* Resolve() {
  uint8_t* base=memory;
  const uint32_t tex_ptr=16;
  const bool trm=false;
  struct { bool retained; } item{retained};
'''+route+r'''
  return nullptr;
}
void Words(uint32_t type,uint32_t address) {
  uint32_t w[6]={type,address,0x7fe0ff,0xd10,0x200,0xb3346a00};
  for (auto& value:w) value=SwapU32(value);
  std::memcpy(memory+16+28,w,sizeof(w));
}
const GuestTexture* Reuse(uint32_t tex_ptr,uint64_t piece,uint64_t frame_number,bool retained=false) {
  constexpr bool second_vr_eye=true;
  constexpr uint32_t skey=7;
  const bool prepare_item=!retained && piece;
  struct { unsigned material_reuses=0; } eye;
  auto* vr_eye=&eye;
  auto site_it=g_r.tex_sticky.end();
  bool tex_pending=true;
'''+prepared+r'''
  return nullptr;
}
int main() {
  auto& site=g_r.tex_sticky[7];
  site.prepared=&g_r.white; site.piece=13; site.last_used_frame=5; site.prepared_object=16;
  assert(Reuse(16,13,5)==&g_r.white);
  assert(!Reuse(17,13,5) && !Reuse(16,14,5) && !Reuse(16,13,6) && !Reuse(16,13,5,true));
  ++g_r.texture_revision;
  assert(!Reuse(16,13,5));
  Words(2,0x12345052);
  Resolve(); assert(reads==2 && g_r.tex_routes.size()==1);
  auto& route=g_r.tex_routes.at(16);
  auto first=route.key;
  // Shared surfaces and the second eye see one consistent frame snapshot.
  Words(2,0x23456052);
  for(int i=0;i<10000;++i) Resolve();
  assert(reads==2 && route.key==first);
  ++frame_number; Resolve();
  assert(reads==4 && route.key!=first);
  first=route.key;
  // A demoted mip keeps the old decode and suspends payload polling.
  Words(2,0x52); ++frame_number; Resolve();
  assert(route.demoted && route.key==first);
  Words(2,0x34567052); ++frame_number; Resolve();
  assert(!route.demoted && route.key!=first);
  first=route.key;
  // Stable foreign bytes must not replace the previous texture route.
  for(uint32_t invalid:{0u,0x407c3180u,0x64657374u}) {
    Words(invalid,0x45678052); ++frame_number; Resolve();
    assert(route.key==first && route.demoted);
  }
  // Actual failed snapshots from the moving test: type 2, foreign format 32.
  for(uint32_t address:{0x407cb220u,0x4063e060u}) {
    Words(0x84000002,address); ++frame_number; Resolve();
    assert(route.key==first && route.demoted);
  }
  readable=false; ++frame_number; Resolve();
  const int failed_reads=reads;
  for(int i=0;i<10;++i) Resolve();
  assert(reads==failed_reads && route.key==first && route.demoted);
  readable=true;
  Words(2,0x45678052); retained=true; ++frame_number; Resolve();
  assert(reads==failed_reads && route.key==first);
  retained=false; Resolve();
  assert(reads==failed_reads+2 && route.key!=first && !route.demoted);
  first=route.key; changing=true; ++frame_number;
  const int before_unstable=reads; Resolve();
  assert(reads==before_unstable+8 && route.key==first && route.demoted);
  changing=false; ++frame_number; Resolve();
  assert(route.key==first && !route.demoted);
  // A failed first read must recover as soon as live words arrive.
  g_r.tex_routes.clear(); Words(0,0); ++frame_number; Resolve();
  assert(g_r.tex_routes.empty());
  Words(2,0x56789052); Resolve(); assert(g_r.tex_routes.size()==1);
  puts("PASS: one stable read per frame; next-frame updates, demote/re-promote, freed/unreadable objects, retained items and recovery");
}
'''
(root / 'out/test-temp').mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=root / 'out/test-temp') as directory:
    cpp = Path(directory) / 'routes.cpp'
    exe = Path(directory) / 'routes.exe'
    cpp.write_text(code)
    subprocess.run(['clang++', '-std=c++20', '-O2', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
