#include "openxr_gamepad.h"
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace skate3::vr;
namespace {
void Check(bool ok) { if(!ok) throw std::runtime_error("Controller check failed"); }
std::vector<XrActionCreateInfo> actions;
std::map<XrPath,std::string> paths;
std::map<XrAction,std::string> names;
bool active=true,attached=false,fail_attach=false;
int destroyed=0,syncs=0;
int spaces_created=0,spaces_destroyed=0,locations=0;
bool fail_second_space=false;
XrSpaceLocationFlags location_flags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
    XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
XrResult sync_result=XR_SUCCESS;
XrActionSet action_set=reinterpret_cast<XrActionSet>(1);
}
extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance,const XrActionSetCreateInfo* info,XrActionSet* set) {
  Check(std::string(info->actionSetName)=="xbox_controls"); *set=action_set; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(XrActionSet set) { Check(set==action_set); ++destroyed; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet set,const XrActionCreateInfo* info,XrAction* action) {
  Check(set==action_set); Check(info->countSubactionPaths==0);
  actions.push_back(*info); *action=reinterpret_cast<XrAction>(actions.size()); names[*action]=info->actionName; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance,const char* path,XrPath* value) {
  *value=paths.size()+1; paths[*value]=path; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance,const XrInteractionProfileSuggestedBinding* info) {
  Check(paths.at(info->interactionProfile)=="/interaction_profiles/oculus/touch_controller");
  Check(info->countSuggestedBindings==15);
  for(uint32_t i=0;i<info->countSuggestedBindings;++i) {
    const auto& binding=info->suggestedBindings[i]; const auto& path=paths.at(binding.binding);
    const auto& name=names.at(binding.action);
    Check(path.starts_with("/user/hand/"));
    if(name=="a") Check(path=="/user/hand/right/input/a/click");
    if(name=="x") Check(path=="/user/hand/left/input/x/click");
    if(name=="start") Check(path=="/user/hand/left/input/menu/click");
    if(name=="left_stick") Check(path=="/user/hand/left/input/thumbstick");
    if(name=="left_aim") Check(path=="/user/hand/left/input/aim/pose");
    if(name=="right_aim") Check(path=="/user/hand/right/input/aim/pose");
  }
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession,const XrSessionActionSetsAttachInfo* info) {
  Check(info->countActionSets==1 && *info->actionSets==action_set);
  if(fail_attach) return XR_ERROR_RUNTIME_FAILURE;
  attached=true; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession,const XrActionsSyncInfo* info) {
  Check(attached && info->countActiveActionSets==1 && info->activeActionSets[0].actionSet==action_set);
  ++syncs; return sync_result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession,const XrActionStateGetInfo* info,XrActionStateBoolean* value) {
  Check(info->subactionPath==XR_NULL_PATH); value->isActive=active;
  value->currentState=names.at(info->action)=="a"; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession,const XrActionStateGetInfo* info,XrActionStateFloat* value) {
  value->isActive=active; value->currentState=names.at(info->action)=="left_trigger" ? 0.5f : 1.f; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession,const XrActionStateGetInfo*,XrActionStateVector2f* value) {
  value->isActive=active; value->currentState={1,-1}; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession,const XrActionSpaceCreateInfo* info,XrSpace* space) {
  Check(names.at(info->action).ends_with("_aim") && info->poseInActionSpace.orientation.w==1);
  if(fail_second_space && spaces_created%2==1) return XR_ERROR_RUNTIME_FAILURE;
  *space=reinterpret_cast<XrSpace>(++spaces_created); return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace) { ++spaces_destroyed; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession,const XrActionStateGetInfo* info,XrActionStatePose* value) {
  Check(names.at(info->action).ends_with("_aim")); value->isActive=active; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace,XrSpace,XrTime time,XrSpaceLocation* value) {
  Check(time==123); ++locations; value->locationFlags=location_flags;
  value->pose={{0,0,0,1},{0.3f,0.4f,-0.5f}}; return XR_SUCCESS;
}
}
int main() {
  TouchControls controls; controls.a=true; controls.trigger[0]=1;
  Check(!MapTouchControls(controls).connected && MapTouchControls(controls).buttons==0);
  controls.connected=true; controls.b=controls.x=controls.y=true;
  controls.grip[0]=controls.grip[1]=1; controls.click[0]=controls.click[1]=true;
  auto pad=MapTouchControls(controls);
  Check(pad.buttons==0xf3c0 && pad.lt==255);
  controls.menu=true; pad=MapTouchControls(controls);
  Check((pad.buttons&0x4030)==0x0020);
  controls.x=false; pad=MapTouchControls(controls); Check((pad.buttons&0x0030)==0x0010);
  controls.stick[0][0]=1; controls.stick[0][1]=-1;
  controls.stick[1][0]=-2; controls.stick[1][1]=2;
  pad=MapTouchControls(controls);
  Check((pad.buttons&0x004f)==0x000a && pad.lx==0 && pad.ly==0 && pad.rx==-32768 && pad.ry==32767);
  controls.stick[0][0]=-1; controls.stick[0][1]=1;
  Check((MapTouchControls(controls).buttons&0x004f)==0x0005);
  controls.click[0]=false; pad=MapTouchControls(controls); Check(pad.lx==-32768 && pad.ly==32767);
  controls.stick[0][0]=std::numeric_limits<float>::quiet_NaN();
  controls.trigger[0]=std::numeric_limits<float>::infinity();
  controls.trigger[1]=-1;
  pad=MapTouchControls(controls); Check(pad.lx==0 && pad.lt==0 && pad.rt==0);
  {
    OpenXrGamepad gamepad(XR_NULL_HANDLE,XR_NULL_HANDLE);
    Check(attached && actions.size()==15 && spaces_created==2);
    Check(actions[5].actionType==XR_ACTION_TYPE_VECTOR2F_INPUT && actions[9].actionType==XR_ACTION_TYPE_FLOAT_INPUT);
    Check(!gamepad.Sample(false).connected && syncs==0);
    Check(!gamepad.LocateHands(XR_NULL_HANDLE,123).aim[0].valid && locations==0);
    pad=MapTouchControls(gamepad.Sample(true));
    Check(pad.connected && pad.buttons==0x1300 && pad.lx==32767 && pad.ly==-32768 && pad.lt==128 && pad.rt==255);
    const auto hands=gamepad.LocateHands(XR_NULL_HANDLE,123);
    Check(hands.aim[0].valid && hands.aim[1].valid && hands.aim[0].pose.position.x==0.3f && locations==2);
    location_flags&=~XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    Check(!gamepad.LocateHands(XR_NULL_HANDLE,123).aim[1].valid);
    active=false; pad=MapTouchControls(gamepad.Sample(true));
    Check(!pad.connected && pad.buttons==0 && pad.lx==0 && pad.lt==0);
    Check(!gamepad.LocateHands(XR_NULL_HANDLE,123).aim[0].valid);
    active=true; sync_result=XR_SESSION_NOT_FOCUSED; Check(!gamepad.Sample(true).connected);
    Check(!gamepad.LocateHands(XR_NULL_HANDLE,123).aim[0].valid);
    sync_result=XR_ERROR_RUNTIME_FAILURE;
    bool threw=false; try { gamepad.Sample(true); } catch(const std::runtime_error&) { threw=true; } Check(threw);
  }
  Check(destroyed==1 && spaces_destroyed==2);
  fail_attach=true;
  bool threw=false;
  try { OpenXrGamepad gamepad(XR_NULL_HANDLE,XR_NULL_HANDLE); } catch(const std::runtime_error&) { threw=true; }
  Check(threw && destroyed==2);
  fail_attach=false; fail_second_space=true; threw=false;
  try { OpenXrGamepad gamepad(XR_NULL_HANDLE,XR_NULL_HANDLE); } catch(const std::runtime_error&) { threw=true; }
  Check(threw && destroyed==3 && spaces_destroyed==3);
}
