#pragma once
#include <openxr/openxr.h>

namespace skate3::vr {
XrPosef RecenterPose(const XrPosef& head);
XrPosef WindowPose(const XrPosef& reference,float distance,float height,float horizontal=0);
void MakeWindowEyeCamera(const float game_view_proj[16],const float game_proj[16],
                         const float game_position[3],const XrPosef& reference,
                         const XrView& eye,float scale,float distance,float view_proj[16],
                         float projection[16],float position[3]);
void MakeHudTransform(const XrPosef& head,const XrView& eye,const float projection[16],
                      float width,float aspect,float distance,float transform[4]);
void MakeEyeCamera(const float game_view_proj[16], const float game_proj[16],
                   const float game_position[3], const XrPosef& reference,
                   const XrView& eye, float world_units_per_meter,
                   float view_proj[16], float projection[16], float position[3],
                   float camera_distance=0, float camera_height=0);
}
