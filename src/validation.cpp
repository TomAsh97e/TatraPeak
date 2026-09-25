#include <tatra/validation.hpp>

#include <cmath>
#include <string>
#include <utility>

namespace tatra {

namespace {

constexpr double kQuaternionNormTolerance = 1e-3;

Error invalidInput(std::string message) {
    return Error{ErrorCode::InvalidInput, std::move(message)};
}

bool isFinite(double value) {
    return std::isfinite(value);
}

} // namespace

Result<GeoCoord> makeGeoCoord(double latitude_deg, double longitude_deg) {
    if (!isFinite(latitude_deg)) {
        return invalidInput("latitude_deg must be finite");
    }
    if (latitude_deg < -90.0 || latitude_deg > 90.0) {
        return invalidInput("latitude_deg must be in the inclusive range [-90, 90]");
    }
    if (!isFinite(longitude_deg)) {
        return invalidInput("longitude_deg must be finite");
    }
    if (longitude_deg < -180.0 || longitude_deg > 180.0) {
        return invalidInput("longitude_deg must be in the inclusive range [-180, 180]");
    }

    return GeoCoord{latitude_deg, longitude_deg};
}

Result<GeoPosition> makeGeoPosition(GeoCoord coord, double ellipsoid_height_m) {
    const auto validated_coord = makeGeoCoord(coord.latitude_deg, coord.longitude_deg);
    if (const auto* error = std::get_if<Error>(&validated_coord)) {
        return *error;
    }
    if (!isFinite(ellipsoid_height_m)) {
        return invalidInput("ellipsoid_height_m must be finite");
    }

    return GeoPosition{std::get<GeoCoord>(validated_coord), ellipsoid_height_m};
}

Result<CameraIntrinsics> validateCamera(CameraIntrinsics camera) {
    if (camera.width_px <= 0) {
        return invalidInput("width_px must be greater than zero");
    }
    if (camera.height_px <= 0) {
        return invalidInput("height_px must be greater than zero");
    }
    if (!isFinite(camera.fx_px) || camera.fx_px <= 0.0) {
        return invalidInput("fx_px must be finite and greater than zero");
    }
    if (!isFinite(camera.fy_px) || camera.fy_px <= 0.0) {
        return invalidInput("fy_px must be finite and greater than zero");
    }
    if (!isFinite(camera.cx_px)) {
        return invalidInput("cx_px must be finite");
    }
    if (!isFinite(camera.cy_px)) {
        return invalidInput("cy_px must be finite");
    }

    return camera;
}

Result<CameraPose> validatePose(CameraPose pose) {
    if (!pose.camera_from_enu.coeffs().allFinite()) {
        return invalidInput("camera_from_enu quaternion coefficients must be finite");
    }

    const double norm = pose.camera_from_enu.norm();
    if (!isFinite(norm) || norm == 0.0) {
        return invalidInput("camera_from_enu quaternion must have a finite, non-zero norm");
    }
    if (std::abs(norm - 1.0) > kQuaternionNormTolerance) {
        return invalidInput("camera_from_enu quaternion norm must be within 0.001 of one");
    }

    pose.camera_from_enu.coeffs() /= norm;
    return pose;
}

Result<QueryOptions> validateOptions(QueryOptions options) {
    if (!isFinite(options.min_range_m) || options.min_range_m < 0.0) {
        return invalidInput("min_range_m must be finite and non-negative");
    }
    if (!isFinite(options.max_range_m)) {
        return invalidInput("max_range_m must be finite");
    }
    if (options.min_range_m >= options.max_range_m) {
        return invalidInput("min_range_m must be less than max_range_m");
    }
    if (!isFinite(options.near_z_m) || options.near_z_m <= 0.0) {
        return invalidInput("near_z_m must be finite and greater than zero");
    }

    const auto& visibility = options.visibility;
    if (!isFinite(visibility.sample_step_m) || visibility.sample_step_m <= 0.0) {
        return invalidInput("visibility.sample_step_m must be finite and greater than zero");
    }
    if (!isFinite(visibility.clearance_band_m) || visibility.clearance_band_m < 0.0) {
        return invalidInput("visibility.clearance_band_m must be finite and non-negative");
    }
    if (!isFinite(visibility.start_guard_m) || visibility.start_guard_m < 0.0) {
        return invalidInput("visibility.start_guard_m must be finite and non-negative");
    }
    if (!isFinite(visibility.target_guard_m) || visibility.target_guard_m < 0.0) {
        return invalidInput("visibility.target_guard_m must be finite and non-negative");
    }
    if (visibility.max_samples == 0) {
        return invalidInput("visibility.max_samples must be greater than zero");
    }

    return options;
}
} // namespace tatra
