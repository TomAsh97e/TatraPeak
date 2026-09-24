#include <tatra/camera.hpp>
#include "not_implemented.hpp"

namespace tatra {
Result<CameraIntrinsics> cameraFromHorizontalFov(int, int, double) {
    return detail::notImplemented("T04", "cameraFromHorizontalFov");
}
Result<CameraPose> poseFromHeadingPitchRoll(double, double, double) {
    return detail::notImplemented("T04", "poseFromHeadingPitchRoll");
}
Projection project(const EnuPoint&, const CameraPose&, const CameraIntrinsics&, double) {
    detail::throwNotImplemented("T04", "project");
}
} // namespace tatra
