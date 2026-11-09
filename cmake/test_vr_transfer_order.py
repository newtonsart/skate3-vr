"""Check the real XR transfer's submission/wait/release and error cleanup."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/vr/game_vr.cpp').read_text()
start=source.index('    auto& eye=eyes[e]; uint32_t index=0;')
prefix=source[start:source.index('      if(capture) PrepareCapture(',start)]
complete=source[source.index('  void CompleteTransfer('):source.index('  void Transfer(')]
tail_start=source.index('      if (!defer_completion)',start)
tail=source[tail_start:source.index('\n  }\n};',tail_start)]
schedule_start=source.index('      if (left_pending) {')
schedule=source[schedule_start:source.index('      projections[eye].pose=',schedule_start)]
cleanup_start=source.index('    if (left_pending) {',source.index('    return rendered;\n  } catch',schedule_start))
cleanup=source[cleanup_start:source.index('    PublishVrControls',cleanup_start)]
code=r'''
#include <cassert>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include "perf_trace.h"
using std::string;
using skate3::vr::TransferTiming;
using FrameClock=std::chrono::steady_clock;
double FrameMs(FrameClock::time_point start, FrameClock::time_point end=FrameClock::now()) {
  return std::chrono::duration<double,std::milli>(end-start).count();
}
std::vector<string> calls;
string failure;
bool gpu_done=true,waited_image=false,drain_throws=false,can_overlap=true;
struct Context { int device=1; unsigned guest_output_width=100,guest_output_height=100; };
struct WindowPointer {};
#define REXLOG_WARN(...) ((void)0)
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
bool NativeRhiCanOverlapNextFrame(int) { return can_overlap; }
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
  TransferTiming transfer_timing[3];
  double gpu_render_ms[2]{},gpu_copy_ms[2]{};
  uint64_t ReadFenceCounter() const { return 0; }
  void ReadEyeTiming(int,uint64_t,uint64_t) { assert(gpu_done); }
  void SaveCapture(unsigned,unsigned,int) { assert(gpu_done); calls.push_back("capture"); }
'''+complete+r'''
  void Transfer(const Context& context,int e,bool capture=false,const void* settings=nullptr,
                float=0,float=1,WindowPointer={},bool defer_completion=false) {
'''+prefix+r'''
      assert(waited_image); calls.push_back("transfer");
      if(failure=="record") throw std::runtime_error("record failed");
      gpu_done=false;
'''+tail+r'''
  }
};
void Run(string fail,std::vector<string> expected,bool ui=false,bool cleanup_throws=false) {
  calls.clear(); failure=fail; gpu_done=true; waited_image=false; drain_throws=cleanup_throws;
  bool threw=false;
  try { State s; s.Transfer({},ui?2:0,false,ui?&s:nullptr); }
  catch(const std::exception& error) {
    threw=true;
    if(cleanup_throws) assert(string(error.what())=="Acquire eye image");
  }
  assert(threw==!fail.empty() && calls==expected);
  if(!cleanup_throws) assert(gpu_done);
}
void Pair(bool overlap,bool capture=false,bool fail_right=false,bool right_yields=false,
          bool share_work=true) {
  calls.clear(); failure.clear(); gpu_done=true; waited_image=false; can_overlap=overlap;
  State s;
  Context context;
  WindowPointer pointer;
  float feather=0,aspect=1;
  bool left_pending=false,overlap_recorded=false;
  bool window=false,photo=false,object_editor=false,pause_menu=false;
  const bool deferred=overlap && !capture && share_work;
  double copy_ms[2]{};
  struct { bool override_camera=true; } camera;
  try {
    for(int eye=0;eye<2;++eye) {
      if (eye) {
        assert(gpu_done == !deferred);
        calls.push_back("prepare-right");
        if (fail_right) throw std::runtime_error("right render failed");
      }
      const bool rendered=!eye || !right_yields;
'''+schedule+r'''
    }
  } catch(const std::exception&) {
'''+cleanup+r'''
    calls.push_back("fallback");
  }
  const std::vector<string> left={"submit","acquire","wait","transfer"};
  const std::vector<string> right=left;
  std::vector<string> expected=left;
  if(deferred) expected.push_back("prepare-right");
  expected.push_back("complete");
  if(capture) expected.push_back("capture");
  expected.push_back("release");
  if(!deferred) expected.push_back("prepare-right");
  if(fail_right) expected.push_back("fallback");
  else if(!right_yields) {
    expected.insert(expected.end(),right.begin(),right.end());
    expected.push_back("complete");
    if(capture) expected.push_back("capture");
    expected.push_back("release");
  }
  assert(calls==expected && gpu_done && !left_pending && overlap_recorded==deferred);
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
  Pair(true);
  Pair(false);
  Pair(true,true);
  Pair(true,false,true);
  Pair(true,false,false,true);
  Pair(true,false,false,false,false);
}
'''
(root/'out/test-temp').mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=root/'out/test-temp') as directory:
    cpp=Path(directory)/'order.cpp'; exe=Path(directory)/'order.exe'
    cpp.write_text(code)
    subprocess.run(['clang++','-std=c++20','-I',str(root/'src/vr'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: right recording overlaps left GPU work; completion precedes release and the next submit; overflow, captures, yields and failures')
