from pathlib import Path
import subprocess, tempfile
s=Path('src/skate3_native_scene.cpp').read_text(encoding='utf-8')
gpu=Path('src/skate3_native_scene_gpu.cpp').read_text(encoding='utf-8')
init=gpu[gpu.index('  bool cull_bounds_valid = num_verts'):gpu.index('  int min_bi =',gpu.index('  bool cull_bounds_valid = num_verts'))]
guard=gpu[gpu.index('    const float cull_pos[3]'):gpu.index('    const auto decode_uv',gpu.index('    const float cull_pos[3]'))]
part=s[s.index('bool CornersOutsideFrustum('):s.index('// Axis-aligned cube of half-extent')]
cull=gpu[gpu.index('  const auto skip_outside_eye = '):gpu.index('  // Opaque items already passed')]
opaque=gpu[gpu.index('    if (skip_outside_eye(item)) continue;'):gpu.index('  // Publish the culled-ctx set')]
code='''#include <cassert>\n#include <cmath>\n#include <limits>\n#include <algorithm>\n#include <array>\n#include <vector>\n#include <random>\n#include <unordered_map>\n#define SKATE3_HAS_OPENXR 1\n#define REXCVAR_GET(name) cull_enabled\nstruct DrawItem { float bbox_min[3], bbox_max[3], world[16]; bool unlit=false,skinned=false,ropa=false,cloth_quads=false; std::vector<float> bones; unsigned mesh=7; unsigned fingerprint=1; };\n'''+part+'''
struct Mesh { bool cull_bounds_valid=false; float cull_bbox_min[3],cull_bbox_max[3]; unsigned fingerprint=1; };
Mesh Bounds(const std::vector<std::array<float,3>>& positions) {
 Mesh out; const auto num_verts=positions.size();
'''+init+'''
 for(auto p:positions) { float x=p[0],y=p[1],z=p[2];
'''+guard+'''
 }
 out.cull_bounds_valid=cull_bounds_valid; return out;
}
bool Cull(DrawItem candidate,Mesh mesh,bool enabled=true,bool vr=true) {
 bool cull_enabled=enabled;
 struct { std::unordered_map<unsigned,Mesh> meshes; } g_r;
 g_r.meshes.emplace(candidate.mesh,mesh);
 struct { bool override_camera=true; unsigned frustum_skips=0; } eye;
 auto* vr_eye=vr?&eye:nullptr;
 struct { float view_proj[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; } scene;
 int distance_checks=0;
 const auto view_dist2=[&](const DrawItem&) { ++distance_checks; return 0.f; };
 std::vector<std::pair<float,const DrawItem*>> opaque_items;
'''+cull+'''
 const std::array items{candidate};
 for (const auto& item : items) {
'''+opaque+'''
 assert(opaque_items.size()==size_t(distance_checks));
 assert(opaque_items.empty()==(eye.frustum_skips==1));
 return opaque_items.empty();
}
'''
code+='''int main() {
 DrawItem it{{-.1f,-.1f,.4f},{.1f,.1f,.6f},{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
 auto b=Bounds({{-.1f,-.2f,.4f},{.1f,.2f,.6f}});
 assert(b.cull_bounds_valid && b.cull_bbox_min[1]==-.2f && b.cull_bbox_max[1]==.2f);
 assert(!Bounds({}).cull_bounds_valid);
 assert(!Bounds({{std::numeric_limits<float>::quiet_NaN(),0,0}}).cull_bounds_valid);
 assert(!Bounds({{0,std::numeric_limits<float>::infinity(),0}}).cull_bounds_valid);
 float eye[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
 assert(!ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[12]=1.15f; assert(!ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[12]=1.4f; assert(ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[12]=0; it.world[13]=1.4f; assert(ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[13]=0; it.world[14]=-1.0f; assert(ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[14]=0; it.world[12]=1.4f;
 float yaw[16]={0,0,-1,0, 0,1,0,0, 1,0,0,0, 0,0,1.5f,1};
 assert(!ItemOutsideFrustum(it,yaw,1.2f,1));
 // Guest bounds can be wrong; actual uploaded geometry must stay visible.
 it.world[12]=2;
 b=Bounds({{-2.1f,0,.4f},{-1.9f,0,.6f}});
 assert(ItemOutsideFrustum(it,eye,1.2f,1));
 assert(!BoundsOutsideFrustum(b.cull_bbox_min,b.cull_bbox_max,it.world,eye,1.2f,1));
 // A broad guest box must not prevent culling a fully offscreen decode.
 it.world[12]=0; it.bbox_min[0]=-100;it.bbox_max[0]=100;
 b=Bounds({{3,0,.4f},{4,0,.6f}});
 assert(!ItemOutsideFrustum(it,eye,1.2f,1));
 assert(BoundsOutsideFrustum(b.cull_bbox_min,b.cull_bbox_max,it.world,eye,1.2f,1));
 assert(Cull(it,b)); assert(!Cull(it,b,false)); assert(!Cull(it,b,true,false));
 auto stale=b; ++stale.fingerprint; assert(!Cull(it,stale));
 it.unlit=true; assert(!Cull(it,b)); it.unlit=false;
 it.skinned=true; assert(!Cull(it,b)); it.skinned=false;
 it.ropa=true; assert(!Cull(it,b)); it.ropa=false;
 it.cloth_quads=true; assert(!Cull(it,b)); it.cloth_quads=false;
 it.bones.resize(12); assert(!Cull(it,b)); it.bones.clear();
 auto invalid=b; invalid.cull_bounds_valid=false; assert(!Cull(it,invalid));
 std::mt19937 rng(17);
 std::uniform_real_distribution<float> value(-3,3);
 for(int trial=0;trial<2000;++trial) {
   std::vector<std::array<float,3>> p;
   for(int i=0;i<32;++i) p.push_back({value(rng),value(rng),value(rng)});
   b=Bounds(p); assert(b.cull_bounds_valid);
   float world[16]={value(rng),0,0,0, 0,value(rng),0,0, 0,0,value(rng),0, value(rng),value(rng),value(rng),1};
   float a=value(rng),view[16]={std::cos(a),0,-std::sin(a),0, 0,1,0,0, std::sin(a),0,std::cos(a),0, 0,0,.5f,1};
   bool visible_vertex=false;
   for(auto point:p) {
     float w[3],clip[4]{};
     for(int k=0;k<3;++k) w[k]=point[0]*world[k]+point[1]*world[4+k]+point[2]*world[8+k]+world[12+k];
     for(int k=0;k<4;++k) clip[k]=w[0]*view[k]+w[1]*view[4+k]+w[2]*view[8+k]+view[12+k];
     visible_vertex |= std::abs(clip[0])<=clip[3] && std::abs(clip[1])<=clip[3] && clip[2]>=0 && clip[2]<=clip[3];
   }
   if(visible_vertex) assert(!BoundsOutsideFrustum(b.cull_bbox_min,b.cull_bbox_max,world,view,1.2f,1));
 }
}
'''
Path('out/test-temp').mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir='out/test-temp') as d:
 p=Path(d)/'test.cpp'; p.write_text(code); exe=Path(d)/'test.exe'
 subprocess.run(['clang++','-std=c++20',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
print('PASS: decoded bounds, stale meshes, early opaque rejection, margins, head turns and 2000 transformed visible-vertex checks')
