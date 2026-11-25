from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/vr/game_vr.cpp').read_text()
native=(root/'src/skate3_native_scene.cpp').read_text()
start=native.index('SceneOverlays SnapshotSceneOverlays()')
overlays=native[start:native.index('void GetCapturedSunDir',start)]
assert s.count('camera.overlays=&overlays;')==2
code=r'''
#include "skate3_native_scene.h"
#include <memory>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <mutex>
namespace skate3::native_scene {
struct Draw2d { std::vector<int> verts; };
struct SplineDraw { std::vector<int> verts; };
std::mutex g_2d_mutex;
std::shared_ptr<const std::vector<Draw2d>> g_scene_2d;
std::shared_ptr<const std::vector<SplineDraw>> g_scene_spline;
'''+overlays+r'''
}
namespace native_scene=skate3::native_scene;
int main() {
  {
    using namespace native_scene;
    g_scene_2d=std::make_shared<const std::vector<Draw2d>>(std::vector<Draw2d>{{{11,12}}});
    g_scene_spline=std::make_shared<const std::vector<SplineDraw>>(std::vector<SplineDraw>{{{17}}});
    const auto pair=SnapshotSceneOverlays();
    assert(pair.hud==g_scene_2d && pair.splines==g_scene_spline);
    g_scene_2d=std::make_shared<const std::vector<Draw2d>>(std::vector<Draw2d>{{{99}}});
    g_scene_spline.reset();
    assert(pair.hud->front().verts.front()==11 && pair.splines->front().verts.front()==17);
    const auto next=SnapshotSceneOverlays();
    assert(next.hud->front().verts.front()==99 && !next.splines);
  }
  native_scene::FrameScene source;
  source.items.resize(2048);
  for (auto& item:source.items) { item.draws.resize(3); item.bones.resize(48,1.f); }
  source.generation=12; source.view_proj[0]=5;
  source.cam_pos[0]=4; source.ui_blur=2;
  source.occl_build_skipped={7,9};
  const auto published=std::make_shared<const native_scene::FrameScene>(source);
  native_scene::FrameSceneView left(*published), right(*published);
  left.view_proj[0]=7; left.cam_pos[0]=0; left.ui_blur=0;
  right.view_proj[0]=8;
  assert(left.generation==12 && right.generation==12);
  assert(published->view_proj[0]==5 && left.view_proj[0]==7 && right.view_proj[0]==8);
  assert(right.cam_pos[0]==4 && right.ui_blur==2);
  assert(left.items.data()==published->items.data() && right.items.data()==left.items.data());
  assert(right.items[0].bones.data()==published->items[0].bones.data());
  assert(right.items[0].draws.data()==published->items[0].draws.data());
  assert(left.occl_build_skipped.data()==published->occl_build_skipped.data());
  assert(left.occl_build_skipped.size()==2 && left.occl_build_skipped[1]==9);
  source.items.clear();
  native_scene::FrameSceneView empty(source);
  assert(empty.items.empty() && right.items.size()==2048);
  puts("PASS: both eye cameras are independent and borrow immutable geometry from the retained frame");
}
'''
(root/'out/test-temp').mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=root/'out/test-temp') as d:
 p=Path(d)/'copy.cpp'; exe=Path(d)/'copy.exe';p.write_text(code)
 subprocess.run(['clang++','-std=c++20','-O2','-I'+str(root/'src'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
