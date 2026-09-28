#include <tatra/terrain.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace tatra {

namespace {

Error invalidInput(const char* message) { return Error{ErrorCode::InvalidInput, message}; }

bool isFinite(double value) { return std::isfinite(value); }

} // namespace

GridTerrain::GridTerrain(GridGeometry geometry, std::vector<float> heights,
                         std::vector<std::uint8_t> mask)
    : geometry_{geometry}, max_row_{static_cast<double>(geometry.rows - 1)},
      max_column_{static_cast<double>(geometry.columns - 1)},
      last_latitude_deg_{geometry.first_center_latitude_deg + max_row_ * geometry.latitude_step_deg},
      last_longitude_deg_{geometry.first_center_longitude_deg +
                          max_column_ * geometry.longitude_step_deg},
      heights_ellipsoid_m_{std::move(heights)}, valid_mask_{std::move(mask)} {}

Result<GridTerrain> GridTerrain::create(GridGeometry geometry,
                                        std::vector<float> heights_ellipsoid_m,
                                        std::vector<std::uint8_t> valid_mask) {
    if (geometry.rows < 2) {
        return invalidInput("grid rows must be at least 2");
    }
    if (geometry.columns < 2) {
        return invalidInput("grid columns must be at least 2");
    }
    if (geometry.rows > std::numeric_limits<std::size_t>::max() / geometry.columns) {
        return invalidInput("grid rows*columns overflows size_t");
    }

    const std::size_t sample_count = geometry.rows * geometry.columns;
    if (heights_ellipsoid_m.size() != sample_count) {
        return invalidInput("heights_ellipsoid_m size must equal rows*columns");
    }
    if (valid_mask.size() != sample_count) {
        return invalidInput("valid_mask size must equal rows*columns");
    }
    if (!isFinite(geometry.first_center_latitude_deg)) {
        return invalidInput("first_center_latitude_deg must be finite");
    }
    if (!isFinite(geometry.first_center_longitude_deg)) {
        return invalidInput("first_center_longitude_deg must be finite");
    }
    if (!isFinite(geometry.latitude_step_deg) || geometry.latitude_step_deg >= 0.0) {
        return invalidInput("latitude_step_deg must be finite and negative for a north-up grid");
    }
    if (!isFinite(geometry.longitude_step_deg) || geometry.longitude_step_deg <= 0.0) {
        return invalidInput("longitude_step_deg must be finite and positive");
    }

    const double last_latitude_deg =
        geometry.first_center_latitude_deg +
        static_cast<double>(geometry.rows - 1) * geometry.latitude_step_deg;
    const double last_longitude_deg =
        geometry.first_center_longitude_deg +
        static_cast<double>(geometry.columns - 1) * geometry.longitude_step_deg;
    if (!isFinite(last_latitude_deg) || !isFinite(last_longitude_deg)) {
        return invalidInput("grid extent coordinates must be finite");
    }

    for (std::size_t index = 0; index < sample_count; ++index) {
        if (valid_mask[index] != 0 && !std::isfinite(heights_ellipsoid_m[index])) {
            return invalidInput("every valid height sample must be finite");
        }
    }

    return GridTerrain{geometry, std::move(heights_ellipsoid_m), std::move(valid_mask)};
}

HeightResult GridTerrain::sampleHeight(GeoCoord position) const {
    if (!isFinite(position.latitude_deg) || !isFinite(position.longitude_deg)) {
        return TerrainSampleError::OutsideCoverage;
    }

    // create() enforces a north-up grid (negative latitude / positive longitude steps).
    if (position.latitude_deg < last_latitude_deg_ ||
        position.latitude_deg > geometry_.first_center_latitude_deg ||
        position.longitude_deg < geometry_.first_center_longitude_deg ||
        position.longitude_deg > last_longitude_deg_) {
        return TerrainSampleError::OutsideCoverage;
    }

    const double fractional_row = std::clamp(
        (position.latitude_deg - geometry_.first_center_latitude_deg) / geometry_.latitude_step_deg,
        0.0, max_row_);
    const double fractional_column =
        std::clamp((position.longitude_deg - geometry_.first_center_longitude_deg) /
                       geometry_.longitude_step_deg,
                   0.0, max_column_);

    const std::size_t row0 = static_cast<std::size_t>(std::floor(fractional_row));
    const std::size_t column0 = static_cast<std::size_t>(std::floor(fractional_column));
    const std::size_t row1 = std::min(row0 + 1, geometry_.rows - 1);
    const std::size_t column1 = std::min(column0 + 1, geometry_.columns - 1);
    const double row_fraction = fractional_row - static_cast<double>(row0);
    const double column_fraction = fractional_column - static_cast<double>(column0);

    const std::size_t rows[4]{row0, row0, row1, row1};
    const std::size_t columns[4]{column0, column1, column0, column1};
    const double weights[4]{
        (1.0 - row_fraction) * (1.0 - column_fraction),
        (1.0 - row_fraction) * column_fraction,
        row_fraction * (1.0 - column_fraction),
        row_fraction * column_fraction,
    };

    double interpolated_height_m = 0.0;
    for (std::size_t sample = 0; sample < 4; ++sample) {
        if (weights[sample] == 0.0) {
            continue;
        }
        const std::size_t index = rows[sample] * geometry_.columns + columns[sample];
        if (valid_mask_[index] == 0) {
            return TerrainSampleError::NoData;
        }
        interpolated_height_m += weights[sample] * static_cast<double>(heights_ellipsoid_m_[index]);
    }

    return HeightSample{interpolated_height_m};
}
} // namespace tatra
