#include <tatra/terrain.hpp>
#include "not_implemented.hpp"
#include <utility>

namespace tatra {
GridTerrain::GridTerrain(GridGeometry geometry, std::vector<float> heights,
    std::vector<std::uint8_t> mask)
    : geometry_{geometry}, heights_ellipsoid_m_{std::move(heights)}, valid_mask_{std::move(mask)} {}

Result<GridTerrain> GridTerrain::create(GridGeometry, std::vector<float>,
    std::vector<std::uint8_t>) {
    return detail::notImplemented("T05", "GridTerrain::create");
}
HeightResult GridTerrain::sampleHeight(GeoCoord) const {
    detail::throwNotImplemented("T05", "GridTerrain::sampleHeight");
}
} // namespace tatra
