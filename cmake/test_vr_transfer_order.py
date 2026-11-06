"""Check the real XR transfer's submission/wait/release and error cleanup."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/vr/game_vr.cpp').read_text()
start=source.index('    auto& eye=eyes[e]; uint32_t index=0;')
prefix=source[start:source.index('      if(capture) PrepareCapture(',start)]
complete_start=source.index('      // Correctness baseline:',start)
complete=source[complete_start:source.index('      if (profile) {',complete_start)]
tail_start=source.index('      XrSwapchainImageReleaseInfo release',complete_start)
tail=source[tail_start:source.index('\n  }\n};',tail_start)]
code=r'''
#include <cassert>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using std::string;
std::vector<string> calls;
string failure;
bool gpu_done=true,waited_image=false,drain_throws=false;
struct Context { int device=1; };
struct Queue {};
struct Gpu { Queue AcquireQueue(int,int) { return {}; } int queue_family_graphics_compute() { return 0; } };
struct XrSwapchainImageAcquireInfo { int type; };
struct XrSwapchainImageWaitInfo { int type; int64_t timeout; };
struct XrSwapchainImageReleaseInfo { int type; };
constexpr int XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO=1,XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO=2,XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO=3;
constexpr int64_t XR_INFINITE_DURATION=-1;
constexpr int VK_TRUE=1;
void XrCheck(int status,const char* message) { if(status) throw std::runtime_error(message); }
void VkCheck(int status,const char* message) { XrCheck(status,message); }
namespace vulkan {
bool NativeRhiFlushForExternal(int,bool wait=true) {
  calls.push_back(wait?"drain":"submit");
  if(wait) { if(drain_throws) throw std::runtime_error("drain failed"); gpu_done=true; return true; }
  if(failure=="submit") return false;
  gpu_done=false; return true;
}
}
int xrAcquireSwapchainImage(int,XrSwapchainImageAcquireInfo*,uint32_t* index) {
  calls.push_back("acquire"); *index=0; return failure=="acquire";
}
int xrWaitSwapchainImage(int,XrSwapchainImageWaitInfo*) {
  calls.push_back("wait"); if(failure=="wait") return 1;
  waited_image=true; return 0;
}
int xrReleaseSwapchainImage(int,XrSwapchainImageReleaseInfo*) {
  assert(waited_image && gpu_done); calls.push_back("release"); return 0;
}
int vkWaitForFences(int,int,int*,int,uint64_t) {
  calls.push_back("complete"); if(failure=="complete") return 1;
  gpu_done=true; return 0;
}
struct State {
  Gpu graphics; Gpu* gpu=&graphics;
  struct Eye { int handle=1; } eyes[3];
  int device=1,fence=1; bool timing=true;
  double transfer_submit_ms[3]{},transfer_image_ms[3]{},transfer_fence_ms[3]{};
  void Transfer(const Context& context,int e,const void* settings=nullptr) {
'''+prefix+r'''
      assert(waited_image); calls.push_back("transfer");
      if(failure=="record") throw std::runtime_error("record failed");
'''+complete+tail+r'''
  }
};
void Run(string fail,std::vector<string> expected,bool ui=false,bool cleanup_throws=false) {
  calls.clear(); failure=fail; gpu_done=true; waited_image=false; drain_throws=cleanup_throws;
  bool threw=false;
  try { State s; s.Transfer({},ui?2:0,ui?&s:nullptr); }
  catch(const std::exception& error) {
    threw=true;
    if(cleanup_throws) assert(string(error.what())=="Acquire eye image");
  }
  assert(threw==!fail.empty() && calls==expected);
  if(!cleanup_throws) assert(gpu_done);
}
int main() {
  Run("",{"submit","acquire","wait","transfer","complete","release"});
  Run("",{"acquire","wait","transfer","complete","release"},true);
  Run("submit",{"submit"});
  Run("acquire",{"submit","acquire","drain"});
  Run("wait",{"submit","acquire","wait","drain"});
  Run("record",{"submit","acquire","wait","transfer","drain","release"});
  Run("complete",{"submit","acquire","wait","transfer","complete","drain","release"});
  Run("acquire",{"acquire"},true);
  Run("acquire",{"submit","acquire","drain"},false,true);
}
'''
(root/'out/test-temp').mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=root/'out/test-temp') as directory:
    cpp=Path(directory)/'order.cpp'; exe=Path(directory)/'order.exe'
    cpp.write_text(code)
    subprocess.run(['clang++','-std=c++20',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: render submission precedes XR wait; completion precedes release; UI skips submission; failures drain pending GPU work before fallback')
