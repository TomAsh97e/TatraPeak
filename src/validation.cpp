#include <tatra/validation.hpp>
#include "not_implemented.hpp"

namespace tatra {
Result<GeoCoord> makeGeoCoord(double, double) {
    return detail::notImplemented("T02", "makeGeoCoord");
}
Result<GeoPosition> makeGeoPosition(GeoCoord, double) {
    return detail::notImplemented("T02", "makeGeoPosition");
}
Result<CameraIntrinsics> validateCamera(CameraIntrinsics) {
    return detail::notImplemented("T02", "validateCamera");
}
Result<CameraPose> validatePose(CameraPose) {
    return detail::notImplemented("T02", "validatePose");
}
Result<QueryOptions> validateOptions(QueryOptions) {
    return detail::notImplemented("T02", "validateOptions");
}
} // namespace tatra
