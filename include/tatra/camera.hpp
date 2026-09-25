#pragma once
#include <tatra/types.hpp>

namespace tatra {

// Creates square-pixel pinhole intrinsics. The principal point is at the
// geometric image centre in the edge-based pixel coordinate convention.
[[nodiscard]] Result<CameraIntrinsics> cameraFromHorizontalFov(int width_px, int height_px,
                                                               double horizontal_fov_deg);

// Heading is clockwise from north, positive pitch looks up, and positive roll
// rotates the camera-right basis vector toward camera-down. The returned
// quaternion maps ENU vectors to camera coordinates (+x right, +y down,
// +z forward).
[[nodiscard]] Result<CameraPose> poseFromHeadingPitchRoll(double heading_deg, double pitch_deg,
                                                          double roll_deg);

enum class ProjectionState { InFrame, OutsideFrame, BehindNearPlane };

struct Projection {
    ProjectionState state = ProjectionState::BehindNearPlane;
    std::optional<PixelPoint> pixel;
    double camera_depth_m = 0.0;
};

// Projects with u=fx*x/z+cx and v=fy*y/z+cy. The pose, camera, and near plane
// must be validated by the caller. Pixels are retained for OutsideFrame, but
// not computed for points at or behind the near plane.
[[nodiscard]] Projection project(const EnuPoint& point, const CameraPose& pose,
                                 const CameraIntrinsics& camera, double near_z_m);

} // namespace tatra
