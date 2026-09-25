#pragma once
#include <tatra/types.hpp>

namespace tatra {

// WGS84 geodetic coordinates use ellipsoidal height. ECEF axes are global
// Cartesian axes in metres.
[[nodiscard]] EcefPoint toEcef(const GeoPosition& position);
[[nodiscard]] GeoPosition fromEcef(const EcefPoint& point);

// Local Cartesian frame at origin: +x east, +y north, +z up, all in metres.
class LocalFrame {
public:
    explicit LocalFrame(const GeoPosition& origin);
    [[nodiscard]] EnuPoint toEnu(const GeoPosition& position) const;
    [[nodiscard]] GeoPosition fromEnu(const EnuPoint& point) const;
private:
    GeoPosition origin_;
};

struct BearingElevation {
    double azimuth_deg = 0.0;
    double elevation_deg = 0.0;
    double slant_range_m = 0.0;
};

// For ENU (east, north, up), azimuth is atan2(east, north) normalized to
// [0, 360), and elevation is atan2(up, hypot(east, north)). A vertical vector
// has the conventional azimuth 0 degrees. The zero vector is invalid.
[[nodiscard]] Result<BearingElevation> bearingElevation(const EnuPoint& point);

} // namespace tatra
