#include "openxr_gamepad.h"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace skate3::vr {
namespace {
void Check(XrResult result,const char* operation) {
  if (XR_FAILED(result)) throw std::runtime_error(std::string(operation)+": XR "+std::to_string(result));
}
}
OpenXrGamepad::OpenXrGamepad(XrInstance instance,XrSession session):session_(session) {
  XrActionSetCreateInfo info{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::snprintf(info.actionSetName,sizeof(info.actionSetName),"xbox_controls");
  std::snprintf(info.localizedActionSetName,sizeof(info.localizedActionSetName),"Xbox controls");
  Check(xrCreateActionSet(instance,&info,&set_),"Create controller action set");
  try {
    struct Binding { const char* name; const char* label; const char* path; XrActionType type; };
    const Binding bindings[Count]={
      {"a","A","/user/hand/right/input/a/click",XR_ACTION_TYPE_BOOLEAN_INPUT},
      {"b","B","/user/hand/right/input/b/click",XR_ACTION_TYPE_BOOLEAN_INPUT},
      {"x","X","/user/hand/left/input/x/click",XR_ACTION_TYPE_BOOLEAN_INPUT},
      {"y","Y","/user/hand/left/input/y/click",XR_ACTION_TYPE_BOOLEAN_INPUT},
      {"start","Start / Back with X","/user/hand/left/input/menu/click",XR_ACTION_TYPE_BOOLEAN_INPUT},
      {"left_stick","Left stick","/user/hand/left/input/thumbstick",XR_ACTION_TYPE_VECTOR2F_INPUT},
      {"right_stick","Right stick","/user/hand/right/input/thumbstick",XR_ACTION_TYPE_VECTOR2F_INPUT},
      {"left_click","L3 / D-pad with tilt","/user/hand/left/input/thumbstick/click",XR_ACTION_TYPE_BOOLEAN_INPUT},
      {"right_click","R3","/user/hand/right/input/thumbstick/click",XR_ACTION_TYPE_BOOLEAN_INPUT},
      {"left_trigger","LT","/user/hand/left/input/trigger/value",XR_ACTION_TYPE_FLOAT_INPUT},
      {"right_trigger","RT","/user/hand/right/input/trigger/value",XR_ACTION_TYPE_FLOAT_INPUT},
      {"left_grip","LB","/user/hand/left/input/squeeze/value",XR_ACTION_TYPE_FLOAT_INPUT},
      {"right_grip","RB","/user/hand/right/input/squeeze/value",XR_ACTION_TYPE_FLOAT_INPUT},
      {"left_aim","Left window pointer","/user/hand/left/input/aim/pose",XR_ACTION_TYPE_POSE_INPUT},
      {"right_aim","Right window pointer","/user/hand/right/input/aim/pose",XR_ACTION_TYPE_POSE_INPUT}
    };
    XrActionSuggestedBinding suggested[Count]{};
    for (int i=0;i<Count;++i) {
      XrActionCreateInfo action{XR_TYPE_ACTION_CREATE_INFO};
      action.actionType=bindings[i].type;
      std::snprintf(action.actionName,sizeof(action.actionName),"%s",bindings[i].name);
      std::snprintf(action.localizedActionName,sizeof(action.localizedActionName),"%s",bindings[i].label);
      Check(xrCreateAction(set_,&action,&actions_[i]),"Create controller action");
      suggested[i].action=actions_[i];
      Check(xrStringToPath(instance,bindings[i].path,&suggested[i].binding),"Resolve controller binding");
    }
    XrInteractionProfileSuggestedBinding profile{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    Check(xrStringToPath(instance,"/interaction_profiles/oculus/touch_controller",&profile.interactionProfile),"Resolve Touch profile");
    profile.countSuggestedBindings=Count; profile.suggestedBindings=suggested;
    Check(xrSuggestInteractionProfileBindings(instance,&profile),"Suggest Touch controls");
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets=1; attach.actionSets=&set_;
    Check(xrAttachSessionActionSets(session_,&attach),"Attach controller actions");
    for(int hand=0;hand<2;++hand) {
      XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
      space.action=actions_[hand ? RightAim : LeftAim]; space.poseInActionSpace.orientation.w=1;
      Check(xrCreateActionSpace(session_,&space,&aim_spaces_[hand]),"Create controller pointer space");
    }
  } catch (...) {
    for(auto space:aim_spaces_) if(space) xrDestroySpace(space);
    xrDestroyActionSet(set_); set_=XR_NULL_HANDLE; throw;
  }
}
OpenXrGamepad::~OpenXrGamepad() {
  for(auto space:aim_spaces_) if(space) xrDestroySpace(space);
  if(set_) xrDestroyActionSet(set_);
}
TouchControls OpenXrGamepad::Sample(bool focused) {
  TouchControls controls;
  focused_=false;
  if (!focused) return controls;
  XrActiveActionSet active{set_,XR_NULL_PATH};
  XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
  sync.countActiveActionSets=1; sync.activeActionSets=&active;
  const auto result=xrSyncActions(session_,&sync);
  if (result==XR_SESSION_NOT_FOCUSED) return controls;
  Check(result,"Sync controller actions");
  focused_=true;
  auto get=[&](Action action) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO}; info.action=actions_[action]; return info;
  };
  auto boolean=[&](Action action) {
    const auto info=get(action); XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
    Check(xrGetActionStateBoolean(session_,&info,&value),"Read controller button");
    controls.connected|=bool(value.isActive); return value.isActive && value.currentState;
  };
  auto scalar=[&](Action action) {
    const auto info=get(action); XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
    Check(xrGetActionStateFloat(session_,&info,&value),"Read controller trigger");
    controls.connected|=bool(value.isActive); return value.isActive ? value.currentState : 0.f;
  };
  controls.a=boolean(A); controls.b=boolean(B); controls.x=boolean(X); controls.y=boolean(Y);
  controls.menu=boolean(Menu);
  controls.click[0]=boolean(LeftClick); controls.click[1]=boolean(RightClick);
  for (int hand=0;hand<2;++hand) {
    const auto info=get(hand ? RightStick : LeftStick);
    XrActionStateVector2f value{XR_TYPE_ACTION_STATE_VECTOR2F};
    Check(xrGetActionStateVector2f(session_,&info,&value),"Read controller stick");
    controls.connected|=bool(value.isActive);
    if(value.isActive) { controls.stick[hand][0]=value.currentState.x; controls.stick[hand][1]=value.currentState.y; }
    controls.trigger[hand]=scalar(hand ? RightTrigger : LeftTrigger);
    controls.grip[hand]=scalar(hand ? RightGrip : LeftGrip);
  }
  return controls;
}
ControllerHands OpenXrGamepad::LocateHands(XrSpace base,XrTime display_time) {
  ControllerHands hands;
  if(!focused_) return hands;
  constexpr auto tracked=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
      XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
  for(int hand=0;hand<2;++hand) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO}; info.action=actions_[hand ? RightAim : LeftAim];
    XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
    if(XR_FAILED(xrGetActionStatePose(session_,&info,&state)) || !state.isActive) continue;
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if(XR_SUCCEEDED(xrLocateSpace(aim_spaces_[hand],base,display_time,&location)) &&
        (location.locationFlags&tracked)==tracked) hands.aim[hand]={true,location.pose};
  }
  return hands;
}
}
