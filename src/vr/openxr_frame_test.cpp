#include "openxr_session.h"

#include <chrono>
#include <semaphore>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
namespace {
const auto render_thread=std::this_thread::get_id();
std::binary_semaphore wait_started{0}, release_wait{0};
XrSessionState next_event=XR_SESSION_STATE_READY;
int waits=0, begins=0, ends=0;
bool frame_begun=false, session_ended=false, fail_wait=false;

void Check(bool condition) {
  if (!condition) throw std::logic_error("OpenXR frame order check failed");
}
}

XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(XrInstance, XrEventDataBuffer* event) {
  if (next_event==XR_SESSION_STATE_UNKNOWN) return XR_EVENT_UNAVAILABLE;
  *reinterpret_cast<XrEventDataSessionStateChanged*>(event)=
      {XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED, nullptr, XR_NULL_HANDLE, next_event};
  next_event=XR_SESSION_STATE_UNKNOWN;
  return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(XrSession, const XrSessionBeginInfo*) { return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession) {
  Check(!frame_begun);
  session_ended=true;
  return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* state) {
  Check(!frame_begun && !session_ended);
  ++waits;
  if (std::this_thread::get_id()!=render_thread) {
    wait_started.release();
    Check(release_wait.try_acquire_for(2s));
  }
  state->predictedDisplayTime=waits*8'333'333;
  state->predictedDisplayPeriod=8'333'333;
  state->shouldRender=XR_TRUE;
  return fail_wait ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession, const XrFrameBeginInfo*) {
  Check(std::this_thread::get_id()==render_thread && !frame_begun && waits==begins+1);
  ++begins;
  frame_begun=true;
  return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession, const XrFrameEndInfo* info) {
  Check(std::this_thread::get_id()==render_thread && frame_begun && info->displayTime==waits*8'333'333);
  ++ends;
  frame_begun=false;
  return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace, XrSpace, XrTime time, XrSpaceLocation*) {
  Check(std::this_thread::get_id()==render_thread && frame_begun && time==waits*8'333'333);
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(XrSession, const XrViewLocateInfo* info, XrViewState*,
                                          uint32_t, uint32_t* count, XrView*) {
  Check(std::this_thread::get_id()==render_thread && frame_begun && info->displayTime==waits*8'333'333);
  *count=2;
  return XR_SUCCESS;
}

int main() {
  skate3::vr::OpenXrContext context;
  {
    skate3::vr::OpenXrSession session(context);
    skate3::vr::TrackingFrame frame;
    Check(session.PollEvents() && session.BeginFrame(frame));
    Check(!frame.prefetched && frame.display_time==8'333'333);
    session.EndFrame(frame);
    session.PrefetchFrame();
    Check(wait_started.try_acquire_for(2s));
    session.PrefetchFrame();  // Duplicate requests must not start another wait.
    Check(waits==2 && begins==1 && ends==1);
    release_wait.release();
    Check(session.PollEvents() && session.BeginFrame(frame));
    Check(frame.prefetched && frame.display_time==16'666'666);
    session.EndFrame(frame);
    session.PrefetchFrame();
    Check(wait_started.try_acquire_for(2s));
    next_event=XR_SESSION_STATE_STOPPING;
    release_wait.release();
    Check(session.PollEvents() && !session.BeginFrame(frame));
    Check(session_ended && waits==3 && begins==2 && ends==2);
  }
  {
    waits=begins=ends=0;
    session_ended=false;
    next_event=XR_SESSION_STATE_READY;
    skate3::vr::OpenXrSession session(context);
    skate3::vr::TrackingFrame frame;
    Check(session.PollEvents() && session.BeginFrame(frame));
    session.EndFrame(frame);
    fail_wait=true;
    session.PrefetchFrame();
    Check(wait_started.try_acquire_for(2s));
    release_wait.release();
    bool failed=false;
    try { session.BeginFrame(frame); } catch (const std::runtime_error&) { failed=true; }
    Check(failed && waits==2 && begins==1 && ends==1);
  }
}
