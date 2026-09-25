#include <tatra/angles.hpp>
#include <tatra/camera.hpp>

#include <Eigen/Geometry>

#include <cmath>

namespace tatra {

namespace {

Error invalidInput(const char* message) { return Error{ErrorCode::InvalidInput, message}; }

} // namespace

Result<CameraIntrinsics> cameraFromHorizontalFov(int width_px, int height_px,
                                                 double horizontal_fov_deg) {
    if (width_px <= 0) {
        return invalidInput("width_px must be greater than zero");
    }
    if (height_px <= 0) {
        return invalidInput("height_px must be greater than zero");
    }
    if (!std::isfinite(horizontal_fov_deg) || horizontal_fov_deg <= 0.0 ||
        horizontal_fov_deg >= 180.0) {
        return invalidInput("horizontal_fov_deg must be finite and in the open range (0, 180)");
    }

    const double width = static_cast<double>(width_px);
    const double height = static_cast<double>(height_px);
    const double half_fov_rad = degreesToRadians(horizontal_fov_deg) / 2.0;
    const double focal_length_px = width / (2.0 * std::tan(half_fov_rad));
    if (!std::isfinite(focal_length_px) || focal_length_px <= 0.0) {
        return invalidInput("horizontal_fov_deg produces a non-finite focal length");
    }

    return CameraIntrinsics{width_px,        height_px,   focal_length_px,
                            focal_length_px, width / 2.0, height / 2.0};
}

Result<CameraPose> poseFromHeadingPitchRoll(double heading_deg, double pitch_deg, double roll_deg) {
    if (!std::isfinite(heading_deg)) {
        return invalidInput("heading_deg must be finite");
    }
    if (!std::isfinite(pitch_deg)) {
        return invalidInput("pitch_deg must be finite");
    }
    if (!std::isfinite(roll_deg)) {
        return invalidInput("roll_deg must be finite");
    }

    const double heading_rad = degreesToRadians(heading_deg);
    const double pitch_rad = degreesToRadians(pitch_deg);
    const double roll_rad = degreesToRadians(roll_deg);

    const double sin_heading = std::sin(heading_rad);
    const double cos_heading = std::cos(heading_rad);
    const double sin_pitch = std::sin(pitch_rad);
    const double cos_pitch = std::cos(pitch_rad);
    const double sin_roll = std::sin(roll_rad);
    const double cos_roll = std::cos(roll_rad);

    const Eigen::Vector3d forward{sin_heading * cos_pitch, cos_heading * cos_pitch, sin_pitch};
    const Eigen::Vector3d right{cos_heading, -sin_heading, 0.0};
    const Eigen::Vector3d down = forward.cross(right);
    const Eigen::Vector3d right_rolled = cos_roll * right + sin_roll * down;
    const Eigen::Vector3d down_rolled = -sin_roll * right + cos_roll * down;

    Eigen::Matrix3d camera_from_enu;
    camera_from_enu.row(0) = right_rolled.transpose();
    camera_from_enu.row(1) = down_rolled.transpose();
    camera_from_enu.row(2) = forward.transpose();

    Eigen::Quaterniond quaternion{camera_from_enu};
    quaternion.normalize();
    return CameraPose{quaternion};
}

Projection project(const EnuPoint& point, const CameraPose& pose, const CameraIntrinsics& camera,
                   double near_z_m) {
    const Eigen::Vector3d camera_point = pose.camera_from_enu * point.value_m;
    const double camera_depth_m = camera_point.z();
    if (camera_depth_m <= near_z_m) {
        return Projection{ProjectionState::BehindNearPlane, std::nullopt, camera_depth_m};
    }

    const double u = camera.fx_px * camera_point.x() / camera_depth_m + camera.cx_px;
    const double v = camera.fy_px * camera_point.y() / camera_depth_m + camera.cy_px;
    const PixelPoint pixel{u, v};
    const bool in_frame = u >= 0.0 && u < static_cast<double>(camera.width_px) && v >= 0.0 &&
                          v < static_cast<double>(camera.height_px);
    return Projection{in_frame ? ProjectionState::InFrame : ProjectionState::OutsideFrame, pixel,
                      camera_depth_m};
}
} // namespace tatra
