#pragma once
#include <tatra/types.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tatra {

enum class TerrainSampleError { OutsideCoverage, NoData };
struct HeightSample { double ellipsoid_height_m = 0.0; };
using HeightResult = std::variant<HeightSample, TerrainSampleError>;

class ITerrain {
public:
    virtual ~ITerrain() = default;
    [[nodiscard]] virtual HeightResult sampleHeight(GeoCoord position) const = 0;
};

struct GridGeometry {
    std::size_t rows = 0;
    std::size_t columns = 0;
    double first_center_latitude_deg = 0.0;
    double first_center_longitude_deg = 0.0;
    double latitude_step_deg = 0.0;  // Negative for north-up grids.
    double longitude_step_deg = 0.0;
};

class GridTerrain final : public ITerrain {
public:
    [[nodiscard]] static Result<GridTerrain> create(GridGeometry geometry,
        std::vector<float> heights_ellipsoid_m, std::vector<std::uint8_t> valid_mask);
    [[nodiscard]] HeightResult sampleHeight(GeoCoord position) const override;
private:
    GridTerrain(GridGeometry geometry, std::vector<float> heights_ellipsoid_m,
        std::vector<std::uint8_t> valid_mask);
    GridGeometry geometry_;
    std::vector<float> heights_ellipsoid_m_;
    std::vector<std::uint8_t> valid_mask_;
};

} // namespace tatra
