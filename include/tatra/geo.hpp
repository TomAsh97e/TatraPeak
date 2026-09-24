#pragma once
#include <tatra/types.hpp>

namespace tatra {

// TODO(T03): WGS84 conversions via GeographicLib.
[[nodiscard]] EcefPoint toEcef(const GeoPosition& position);
[[nodiscard]] GeoPosition fromEcef(const EcefPoint& point);

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

[[nodiscard]] Result<BearingElevation> bearingElevation(const EnuPoint& point);

} // namespace tatra
