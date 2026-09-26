#include <GeographicLib/Geoid.hpp>
#include "file_hash.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <tatra/desktop/io.hpp>
#include <utility>
#include <vector>

namespace tatra::desktop {
namespace {

constexpr std::string_view kEgm2008Reference = "EGM2008 (EPSG:3855)";

Error invalid(std::string message) {
    return {ErrorCode::InvalidInput, std::move(message)};
}

bool nearlyEqual(double first, double second, double scale = 1.0) {
    return std::abs(first - second) <= 1e-10 * std::max({scale, std::abs(first),
                                                         std::abs(second)});
}

Result<DemGeoTiff> mosaicAndCrop(const std::vector<DemGeoTiff>& inputs,
                                 const PrepareOptions::Bounds& bounds) {
    if (inputs.empty())
        return invalid("At least one raw DEM is required");
    const auto& reference = inputs.front();
    const double longitude_step = reference.geometry.longitude_step_deg;
    const double latitude_step = reference.geometry.latitude_step_deg;
    const double row_step = -latitude_step;
    double anchor_longitude = reference.geometry.first_center_longitude_deg;
    double anchor_latitude = reference.geometry.first_center_latitude_deg;
    for (const auto& input : inputs) {
        if (!nearlyEqual(input.geometry.longitude_step_deg, longitude_step) ||
            !nearlyEqual(input.geometry.latitude_step_deg, latitude_step) ||
            input.projection_wkt != reference.projection_wkt)
            return Error{ErrorCode::UnsupportedFormat,
                         "Raw DEM tiles must use the same WGS84 grid"};
        anchor_longitude =
            std::min(anchor_longitude, input.geometry.first_center_longitude_deg);
        anchor_latitude = std::max(anchor_latitude, input.geometry.first_center_latitude_deg);
    }

    struct LocatedInput {
        const DemGeoTiff* raster = nullptr;
        std::int64_t row_offset = 0;
        std::int64_t column_offset = 0;
    };
    std::vector<LocatedInput> located;
    std::int64_t minimum_row = std::numeric_limits<std::int64_t>::max();
    std::int64_t minimum_column = std::numeric_limits<std::int64_t>::max();
    std::int64_t maximum_row = std::numeric_limits<std::int64_t>::min();
    std::int64_t maximum_column = std::numeric_limits<std::int64_t>::min();
    for (const auto& input : inputs) {
        const double row_offset_exact =
            (anchor_latitude - input.geometry.first_center_latitude_deg) / row_step;
        const double column_offset_exact =
            (input.geometry.first_center_longitude_deg - anchor_longitude) / longitude_step;
        const auto row_offset = static_cast<std::int64_t>(std::llround(row_offset_exact));
        const auto column_offset = static_cast<std::int64_t>(std::llround(column_offset_exact));
        if (!nearlyEqual(row_offset_exact, static_cast<double>(row_offset), 1.0) ||
            !nearlyEqual(column_offset_exact, static_cast<double>(column_offset), 1.0))
            return Error{ErrorCode::UnsupportedFormat,
                         "Raw DEM tiles are not aligned on one natural grid"};
        located.push_back({&input, row_offset, column_offset});
        minimum_row = std::min(minimum_row, row_offset);
        minimum_column = std::min(minimum_column, column_offset);
        maximum_row = std::max(
            maximum_row, row_offset + static_cast<std::int64_t>(input.geometry.rows) - 1);
        maximum_column = std::max(
            maximum_column,
            column_offset + static_cast<std::int64_t>(input.geometry.columns) - 1);
    }

    constexpr double index_tolerance = 1e-7;
    std::int64_t first_column = static_cast<std::int64_t>(
        std::ceil((bounds.west_deg - anchor_longitude) / longitude_step - index_tolerance));
    std::int64_t last_column = static_cast<std::int64_t>(
        std::floor((bounds.east_deg - anchor_longitude) / longitude_step + index_tolerance));
    std::int64_t first_row = static_cast<std::int64_t>(
        std::ceil((anchor_latitude - bounds.north_deg) / row_step - index_tolerance));
    std::int64_t last_row = static_cast<std::int64_t>(
        std::floor((anchor_latitude - bounds.south_deg) / row_step + index_tolerance));
    first_column = std::max(first_column, minimum_column);
    last_column = std::min(last_column, maximum_column);
    first_row = std::max(first_row, minimum_row);
    last_row = std::min(last_row, maximum_row);
    if (first_column > last_column || first_row > last_row)
        return invalid("Requested bounds do not intersect the raw DEM coverage");

    const auto rows = static_cast<std::size_t>(last_row - first_row + 1);
    const auto columns = static_cast<std::size_t>(last_column - first_column + 1);
    if (rows > std::numeric_limits<std::size_t>::max() / columns)
        return invalid("Cropped DEM dimensions overflow size_t");
    const std::size_t count = rows * columns;
    DemGeoTiff output;
    output.geometry = {rows,
                       columns,
                       anchor_latitude - static_cast<double>(first_row) * row_step,
                       anchor_longitude + static_cast<double>(first_column) * longitude_step,
                       latitude_step,
                       longitude_step};
    output.geo_transform = {
        output.geometry.first_center_longitude_deg - 0.5 * longitude_step,
        longitude_step,
        0.0,
        output.geometry.first_center_latitude_deg - 0.5 * latitude_step,
        0.0,
        latitude_step};
    output.projection_wkt = reference.projection_wkt;
    output.area_or_point = "Point";
    output.vertical_reference = std::string{kEgm2008Reference};
    output.height_values_m.resize(count, 0.0F);
    output.valid_mask.resize(count, 0);
    std::vector<std::uint8_t> covered(count, 0);

    bool common_no_data = true;
    output.no_data_value = reference.no_data_value;
    for (const auto& item : located) {
        const auto& input = *item.raster;
        if (input.no_data_value != output.no_data_value)
            common_no_data = false;
        for (std::size_t row = 0; row < input.geometry.rows; ++row) {
            const auto global_row = item.row_offset + static_cast<std::int64_t>(row);
            if (global_row < first_row || global_row > last_row)
                continue;
            for (std::size_t column = 0; column < input.geometry.columns; ++column) {
                const auto global_column =
                    item.column_offset + static_cast<std::int64_t>(column);
                if (global_column < first_column || global_column > last_column)
                    continue;
                const auto output_row = static_cast<std::size_t>(global_row - first_row);
                const auto output_column =
                    static_cast<std::size_t>(global_column - first_column);
                const std::size_t output_index = output_row * columns + output_column;
                const std::size_t input_index = row * input.geometry.columns + column;
                const bool input_valid = input.valid_mask[input_index] != 0;
                if (covered[output_index] != 0 && input_valid &&
                    output.valid_mask[output_index] != 0 &&
                    !nearlyEqual(output.height_values_m[output_index],
                                 input.height_values_m[input_index], 1.0))
                    return Error{ErrorCode::CorruptData,
                                 "Overlapping DEM tiles disagree at a shared grid point"};
                covered[output_index] = 255;
                if (input_valid || output.valid_mask[output_index] == 0) {
                    output.height_values_m[output_index] = input.height_values_m[input_index];
                    output.valid_mask[output_index] = input.valid_mask[input_index];
                }
            }
        }
    }
    if (std::any_of(covered.begin(), covered.end(),
                    [](std::uint8_t value) { return value == 0; }))
        return Error{ErrorCode::CorruptData,
                     "Raw DEM tiles do not completely cover the requested natural grid"};
    if (!common_no_data)
        output.no_data_value.reset();
    return output;
}

} // namespace

Status prepareDataset(const PrepareOptions& options) {
    if (options.raw_dem.empty())
        return invalid("At least one --raw-dem file is required");
    if (options.geoid_directory.empty() || options.output_directory.empty())
        return invalid("Geoid and output directories are required");
    if (options.geoid_model.empty())
        return invalid("Geoid model name must not be empty");
    const auto& bounds = options.bounds;
    if (!std::isfinite(bounds.west_deg) || !std::isfinite(bounds.south_deg) ||
        !std::isfinite(bounds.east_deg) || !std::isfinite(bounds.north_deg) ||
        bounds.west_deg < -180.0 || bounds.east_deg > 180.0 ||
        bounds.south_deg < -90.0 || bounds.north_deg > 90.0 ||
        bounds.west_deg >= bounds.east_deg || bounds.south_deg >= bounds.north_deg)
        return invalid("Bounds must be finite WGS84 west south east north values");

    std::vector<DemGeoTiff> raw_tiles;
    raw_tiles.reserve(options.raw_dem.size());
    std::vector<std::string> source_hashes;
    source_hashes.reserve(options.raw_dem.size());
    for (const auto& path : options.raw_dem) {
        auto loaded = readDemGeoTiff(path);
        if (const auto* error = std::get_if<Error>(&loaded))
            return *error;
        raw_tiles.push_back(std::get<DemGeoTiff>(std::move(loaded)));
        auto hash = detail::sha256File(path);
        if (const auto* error = std::get_if<Error>(&hash))
            return *error;
        source_hashes.push_back(std::get<std::string>(std::move(hash)));
    }
    auto mosaicked = mosaicAndCrop(raw_tiles, bounds);
    if (const auto* error = std::get_if<Error>(&mosaicked))
        return *error;
    auto raster = std::get<DemGeoTiff>(std::move(mosaicked));
    for (const auto& path : options.raw_dem)
        raster.source_files.push_back(path.filename().string());
    raster.source_sha256 = std::move(source_hashes);

    const auto geoid_file = options.geoid_directory / (options.geoid_model + ".pgm");
    auto geoid_hash = detail::sha256File(geoid_file);
    if (const auto* error = std::get_if<Error>(&geoid_hash))
        return *error;
    try {
        GeographicLib::Geoid geoid(options.geoid_model, options.geoid_directory.string(), true);
        geoid.CacheArea(bounds.south_deg, bounds.west_deg, bounds.north_deg, bounds.east_deg);
        const GeoidHeightProvider provider = [&geoid](GeoCoord position) -> Result<double> {
            try {
                return geoid(position.latitude_deg, position.longitude_deg);
            } catch (const std::exception& error) {
                return Error{ErrorCode::CorruptData,
                             "Cannot evaluate geoid height: " + std::string{error.what()}};
            }
        };
        auto normalized = normalizeHeightGridToEllipsoid(
            raster, provider, options.geoid_model, std::get<std::string>(std::move(geoid_hash)));
        if (const auto* error = std::get_if<Error>(&normalized))
            return *error;
        std::error_code directory_error;
        std::filesystem::create_directories(options.output_directory, directory_error);
        if (directory_error)
            return Error{ErrorCode::IoFailure,
                         "Cannot create output directory: " + directory_error.message()};
        return writeNormalizedGeoTiff(std::get<DemGeoTiff>(normalized),
                                      options.output_directory / "terrain-ellipsoid.tif");
    } catch (const std::exception& error) {
        return Error{ErrorCode::IoFailure,
                     "Cannot load geoid model " + options.geoid_model + ": " + error.what()};
    }
}

} // namespace tatra::desktop
