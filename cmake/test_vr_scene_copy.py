from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/vr/game_vr.cpp').read_text()
a=s.index('  std::shared_ptr<native_scene::FrameScene> scene_copy;')
b=s.index('  int settings_editor=',a)
method=s[a:b]
code=r'''
#include "skate3_native_scene.h"
#include <memory>
#include <cassert>
#include <chrono>
#include <cstdio>
namespace native_scene=skate3::native_scene;
struct Cache {
'''+method+r'''
};
int main() {
  native_scene::FrameScene source;
  source.items.resize(2048);
  for (auto& item:source.items) { item.draws.resize(3); item.bones.resize(48,1.f); }
  source.generation=12; source.view_proj[0]=5;
  Cache cache;
  auto first=cache.CopyScene(source);
  auto* items=first->items.data(); auto* draws=first->items[0].draws.data();
  auto* bones=first->items[0].bones.data();
  first->view_proj[0]=7; first->items[0].bones[0]=9;
  assert(source.view_proj[0]==5 && source.items[0].bones[0]==1);
  first.reset(); source.generation=13; source.items[0].draws[0].index_count=77;
  auto second=cache.CopyScene(source);
  assert(second->generation==13 && second->view_proj[0]==5);
  assert(second->items[0].bones[0]==1 && second->items[0].draws[0].index_count==77);
  assert(second->items.data()==items && second->items[0].draws.data()==draws && second->items[0].bones.data()==bones);
  second.reset();
  constexpr int count=300;
  using Clock=std::chrono::steady_clock;
  auto start=Clock::now();
  for(int i=0;i<count;++i) {
    auto copy=std::make_shared<native_scene::FrameScene>(source);
    assert(copy->items.size()==2048);
  }
  auto middle=Clock::now();
  for(int i=0;i<count;++i) {
    auto copy=cache.CopyScene(source);
    assert(copy->items.size()==2048);
  }
  auto end=Clock::now();
  printf("Scene-copy CPU only: fresh %.4f ms, reused %.4f ms per frame\n",
    std::chrono::duration<double,std::milli>(middle-start).count()/count,
    std::chrono::duration<double,std::milli>(end-middle).count()/count);
  for(size_t n : {0u,3u,4096u,17u}) {
    source.items.resize(n); source.generation++;
    auto copy=cache.CopyScene(source);
    assert(copy->items.size()==n && copy->generation==source.generation);
  }
  puts("PASS: copied cameras and nested data stay isolated; capacities reused; size changes copied");
}
'''
(root/'out/test-temp').mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=root/'out/test-temp') as d:
 p=Path(d)/'copy.cpp'; exe=Path(d)/'copy.exe';p.write_text(code)
 subprocess.run(['clang++','-std=c++20','-O2','-I'+str(root/'src'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
