from pathlib import Path
import subprocess, tempfile
s=Path('src/skate3_native_scene.cpp').read_text(encoding='utf-8')
gpu=Path('src/skate3_native_scene_gpu.cpp').read_text(encoding='utf-8')
guard=gpu[gpu.index('    if (!(x >= item.bbox_min[0]'):gpu.index('    const auto decode_uv', gpu.index('    if (!(x >= item.bbox_min[0]'))]
part=s[s.index('bool CornersOutsideFrustum('):s.index('// Axis-aligned cube of half-extent')]
code='''#include <cassert>\n#include <cmath>\n#include <limits>\nstruct DrawItem { float bbox_min[3], bbox_max[3], world[16]; };\n'''+part+'''\nbool BoundsContain(DrawItem item,float x,float y,float z) { bool cull_bounds_valid=true;\n'''+guard+'''return cull_bounds_valid; }\n'''
code+='''int main() {
 DrawItem it{{-.1f,-.1f,.4f},{.1f,.1f,.6f},{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
 assert(BoundsContain(it,0,0,.5f)); assert(!BoundsContain(it,.11f,0,.5f)); assert(!BoundsContain(it,std::numeric_limits<float>::quiet_NaN(),0,.5f));
 float eye[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
 assert(!ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[12]=1.15f; assert(!ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[12]=1.4f; assert(ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[12]=0; it.world[13]=1.4f; assert(ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[13]=0; it.world[14]=-1.0f; assert(ItemOutsideFrustum(it,eye,1.2f,1));
 it.world[14]=0; it.world[12]=1.4f;
 float yaw[16]={0,0,-1,0, 0,1,0,0, 1,0,0,0, 0,0,1.5f,1};
 assert(!ItemOutsideFrustum(it,yaw,1.2f,1));
}
'''
Path('out/test-temp').mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir='out/test-temp') as d:
 p=Path(d)/'test.cpp'; p.write_text(code); exe=Path(d)/'test.exe'
 subprocess.run(['clang++','-std=c++20',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
print('PASS: bounds containment, NaN, eye frustum, margins and rotated view')
