"""Compare the real retention pass with its former submitted-set algorithm."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/skate3_native_scene.cpp').read_text()
diagnostics = (root / 'src/native/skate3_native_diagnostics.cpp').read_text()
start = source.index('    const uint64_t now = g_guest_frame;', source.index('// Off-screen retention (see g_retained_items)'))
body = source[start:source.index('    g_pw_bi_retain.Add(', start)]
old = body.replace('    const size_t published = scene.items.size();',
    '    std::unordered_set<uint64_t> submitted;\n    const size_t published = scene.items.size();\n    submitted.reserve(published);')
old = old.replace('      const uint64_t key = SynPanItemKey(it);',
    '      const uint64_t key = SynPanItemKey(it);\n      submitted.insert(key);')
old = old.replace('rit->second.last_seen == now', 'submitted.find(rit->first) != submitted.end()')
assert old != body and 'submitted.insert' in old
math = source[source.index('bool CornersOutsideFrustum('):source.index('// Axis-aligned cube of half-extent')]
key = diagnostics[diagnostics.index('uint64_t SynPanItemKey('):diagnostics.index('double SynPanAngleDeg(')]
retained = source[source.index('struct RetainedItem {'):source.index('std::unordered_map<uint64_t, RetainedItem> g_retained_items;')]
code = r'''
#include "skate3_native_scene.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <unordered_map>
#include <unordered_set>
size_t allocations=0;
void* operator new(size_t n) { ++allocations; if(auto p=std::malloc(n?n:1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
using namespace skate3::native_scene;
'''+retained+'namespace skate3::native_scene {\n'+math+'}\n'+key+r'''
using Cache=std::unordered_map<uint64_t,RetainedItem>;
constexpr float kRetainGuestCullMargin=.97f,kRetainDrawMargin=1.05f;
'''
for name, text in (('Keep', body), ('Old', old)):
    code += f'size_t {name}(FrameScene& scene,Cache& g_retained_items,uint64_t g_guest_frame,const float* guest_vp,float wide_hor_scale=1) {{\nuint64_t retain_appended=0;\n'+text+'return retain_appended;\n}\n'
code += r'''
DrawItem Item(uint32_t mesh,float x=0) {
  DrawItem it{}; it.mesh=mesh; it.vb_obj=mesh+100; it.ib_obj=mesh+200;
  it.fingerprint=mesh; it.draws.push_back({4,0,0,6});
  for(int i=0;i<4;++i) it.world[i*4+i]=1;
  it.world[12]=x;
  for(int i=0;i<3;++i) { it.bbox_min[i]=-.1f; it.bbox_max[i]=.1f; }
  it.bbox_min[2]=.4f; it.bbox_max[2]=.6f;
  return it;
}
void Same(const DrawItem& a,const DrawItem& b) {
  assert(SynPanItemKey(a)==SynPanItemKey(b) && a.fingerprint==b.fingerprint);
  assert(a.retained==b.retained && a.selected==b.selected && a.draws.size()==b.draws.size());
  assert(std::memcmp(a.world,b.world,sizeof(a.world))==0);
}
int main() {
  float guest[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  FrameScene scene; std::copy(guest,guest+16,scene.view_proj);
  scene.view_proj[12]=-2.5f; scene.items={Item(1,2.5f)};
  Cache cache; Keep(scene,cache,1,guest);
  scene.items.clear(); assert(Keep(scene,cache,2,guest)==1);
  assert(scene.items[0].retained && !scene.items[0].selected);
  scene.items.clear(); assert(Keep(scene,cache,91,guest)==1);
  scene.items.clear(); assert(Keep(scene,cache,92,guest)==0 && cache.empty());
  scene.items={Item(1,2.5f)}; Keep(scene,cache,93,guest);
  scene.items.clear(); guest[12]=-2.5f;
  assert(Keep(scene,cache,94,guest)==0 && cache.empty()); guest[12]=0;
  // Identical published/appended objects through movement, turns, streaming,
  // duplicates, dynamic exclusions, cache clears and changing fingerprints.
  Cache before,after; std::mt19937 rng(7);
  for(uint64_t frame=1;frame<=400;++frame) {
    if(frame%101==0) { before.clear(); after.clear(); }
    FrameScene a;
    float angle=float(frame)*.13f;
    std::copy(guest,guest+16,a.view_proj);
    a.view_proj[0]=a.view_proj[10]=std::cos(angle);
    a.view_proj[2]=-std::sin(angle); a.view_proj[8]=std::sin(angle);
    for(uint32_t i=0;i<200;++i) {
      if(rng()%4==0) continue;
      auto it=Item(i,float(int(rng()%60)-30)*.1f);
      it.fingerprint+=frame/16; it.world[13]=float(i%5)*.02f;
      it.skinned=i%31==0; it.ropa=i%29==0; it.cloth_quads=i%37==0;
      it.pending=i%41==0; if(i%43==0) it.bones.resize(12);
      a.items.push_back(it); if(i%17==0) a.items.push_back(it);
    }
    auto b=a;
    assert(Old(a,before,frame,guest)==Keep(b,after,frame,guest));
    assert(a.items.size()==b.items.size() && before.size()==after.size());
    for(size_t i=0;i<a.items.size();++i) Same(a.items[i],b.items[i]);
    for(auto& [k,v]:before) { auto& other=after.at(k); assert(v.last_seen==other.last_seen); Same(v.item,other.item); }
  }
  // At the growth cap existing keys must still be stamped; new ones skip.
  Cache full; for(uint32_t i=0;i<20000;++i) { auto it=Item(i); full.emplace(SynPanItemKey(it),RetainedItem{it,1}); }
  before=full; after=full; FrameScene a; std::copy(guest,guest+16,a.view_proj);
  a.items={Item(3),Item(30000)}; auto b=a;
  assert(Old(a,before,200,guest)==Keep(b,after,200,guest));
  assert(before.size()==after.size() && a.items.size()==b.items.size());
  assert(after.size()==1 && after.at(SynPanItemKey(Item(3))).last_seen==200);
  FrameScene fixed; std::copy(guest,guest+16,fixed.view_proj);
  for(uint32_t i=0;i<2048;++i) fixed.items.push_back(Item(i));
  before.clear(); after.clear(); Old(fixed,before,1,guest); Keep(fixed,after,1,guest);
  using Clock=std::chrono::steady_clock; constexpr uint64_t repeats=300;
  allocations=0; auto start=Clock::now();
  for(uint64_t i=2;i<repeats+2;++i) Old(fixed,before,i,guest);
  auto middle=Clock::now(); auto old_allocations=allocations; allocations=0;
  for(uint64_t i=2;i<repeats+2;++i) Keep(fixed,after,i,guest);
  auto end=Clock::now(); auto new_allocations=allocations;
  assert(old_allocations>=2048*repeats && new_allocations==0);
  printf("Static retention CPU only: old %.4f ms, new %.4f ms; allocations/frame %zu -> %zu\n",
      std::chrono::duration<double,std::milli>(middle-start).count()/repeats,
      std::chrono::duration<double,std::milli>(end-middle).count()/repeats,
      old_allocations/repeats,new_allocations/repeats);
  puts("PASS: same retained objects and lifetimes through camera turns, removal, dynamic exclusions, duplicates, cache clears and growth cap");
}
'''
(root / 'out/test-temp').mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=root / 'out/test-temp') as directory:
    cpp = Path(directory) / 'retention.cpp'
    exe = Path(directory) / 'retention.exe'
    cpp.write_text(code)
    subprocess.run(['clang++', '-std=c++20', '-O2', '-I'+str(root/'src'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
