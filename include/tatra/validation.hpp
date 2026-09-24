#pragma once
#include <tatra/engine.hpp>

namespace tatra {
// TODO(T02): validate domain inputs; do not silently replace invalid values.
[[nodiscard]] Result<GeoCoord> makeGeoCoord(double latitude_deg, double longitude_deg);
[[nodiscard]] Result<GeoPosition> makeGeoPosition(GeoCoord coord, double ellipsoid_height_m);
[[nodiscard]] Result<CameraIntrinsics> validateCamera(CameraIntrinsics camera);
[[nodiscard]] Result<CameraPose> validatePose(CameraPose pose);
[[nodiscard]] Result<QueryOptions> validateOptions(QueryOptions options);
} // namespace tatra
