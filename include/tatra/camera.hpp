#pragma once
#include <tatra/types.hpp>

namespace tatra {

[[nodiscard]] Result<CameraIntrinsics> cameraFromHorizontalFov(
    int width_px, int height_px, double horizontal_fov_deg);
[[nodiscard]] Result<CameraPose> poseFromHeadingPitchRoll(
    double heading_deg, double pitch_deg, double roll_deg);

enum class ProjectionState { InFrame, OutsideFrame, BehindNearPlane };

struct Projection {
    ProjectionState state = ProjectionState::BehindNearPlane;
    std::optional<PixelPoint> pixel;
    double camera_depth_m = 0.0;
};

// TODO(T04): pinhole projection. Parameters must be validated first.
[[nodiscard]] Projection project(const EnuPoint& point, const CameraPose& pose,
    const CameraIntrinsics& camera, double near_z_m);

} // namespace tatra
