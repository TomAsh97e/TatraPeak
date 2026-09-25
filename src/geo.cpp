#include <tatra/geo.hpp>
#include <tatra/angles.hpp>

#include <GeographicLib/Geocentric.hpp>
#include <GeographicLib/LocalCartesian.hpp>

#include <cmath>

namespace tatra {

namespace {

GeographicLib::LocalCartesian makeLocalCartesian(const GeoPosition& origin) {
    return GeographicLib::LocalCartesian{origin.coord.latitude_deg,
        origin.coord.longitude_deg, origin.ellipsoid_height_m};
}

} // namespace

EcefPoint toEcef(const GeoPosition& position) {
    EcefPoint result;
    GeographicLib::Geocentric::WGS84().Forward(position.coord.latitude_deg,
        position.coord.longitude_deg, position.ellipsoid_height_m,
        result.value_m.x(), result.value_m.y(), result.value_m.z());
    return result;
}

GeoPosition fromEcef(const EcefPoint& point) {
    GeoPosition result;
    GeographicLib::Geocentric::WGS84().Reverse(point.value_m.x(), point.value_m.y(),
        point.value_m.z(), result.coord.latitude_deg, result.coord.longitude_deg,
        result.ellipsoid_height_m);
    return result;
}

LocalFrame::LocalFrame(const GeoPosition& origin) : origin_{origin} {}

EnuPoint LocalFrame::toEnu(const GeoPosition& position) const {
    EnuPoint result;
    makeLocalCartesian(origin_).Forward(position.coord.latitude_deg,
        position.coord.longitude_deg, position.ellipsoid_height_m,
        result.value_m.x(), result.value_m.y(), result.value_m.z());
    return result;
}

GeoPosition LocalFrame::fromEnu(const EnuPoint& point) const {
    GeoPosition result;
    makeLocalCartesian(origin_).Reverse(point.value_m.x(), point.value_m.y(),
        point.value_m.z(), result.coord.latitude_deg, result.coord.longitude_deg,
        result.ellipsoid_height_m);
    return result;
}

Result<BearingElevation> bearingElevation(const EnuPoint& point) {
    if (!point.value_m.allFinite()) {
        return Error{ErrorCode::InvalidInput, "ENU direction components must be finite"};
    }

    const double east_m = point.value_m.x();
    const double north_m = point.value_m.y();
    const double up_m = point.value_m.z();
    const double horizontal_range_m = std::hypot(east_m, north_m);
    const double slant_range_m = std::hypot(horizontal_range_m, up_m);
    if (!std::isfinite(slant_range_m)) {
        return Error{ErrorCode::InvalidInput, "ENU direction length must be finite"};
    }
    if (slant_range_m == 0.0) {
        return Error{ErrorCode::InvalidInput, "ENU direction must not be the zero vector"};
    }

    double azimuth_deg = 0.0;
    if (horizontal_range_m != 0.0) {
        azimuth_deg = radiansToDegrees(std::atan2(east_m, north_m));
        if (azimuth_deg < 0.0) {
            azimuth_deg += 360.0;
        }
        // Avoid exposing negative zero as an otherwise valid azimuth.
        if (azimuth_deg == 0.0) {
            azimuth_deg = 0.0;
        }
    }

    const double elevation_deg =
        radiansToDegrees(std::atan2(up_m, horizontal_range_m));
    return BearingElevation{azimuth_deg, elevation_deg, slant_range_m};
}
} // namespace tatra
