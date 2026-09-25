#pragma once
#include <tatra/engine.hpp>

namespace tatra {
// All validation failures use ErrorCode::InvalidInput and name the invalid field.
[[nodiscard]] Result<GeoCoord> makeGeoCoord(double latitude_deg, double longitude_deg);
[[nodiscard]] Result<GeoPosition> makeGeoPosition(GeoCoord coord, double ellipsoid_height_m);
[[nodiscard]] Result<CameraIntrinsics> validateCamera(CameraIntrinsics camera);

// Quaternion norms within 0.1% of one are normalized. Larger deviations are
// rejected so an arbitrary scaling cannot silently become a valid rotation.
[[nodiscard]] Result<CameraPose> validatePose(CameraPose pose);
[[nodiscard]] Result<QueryOptions> validateOptions(QueryOptions options);
} // namespace tatra
