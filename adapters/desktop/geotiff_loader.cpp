#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gdal_priv.h>
#include <limits>
#include <memory>
#include <ogr_spatialref.h>
#include <string>
#include <string_view>
#include <tatra/desktop/io.hpp>
#include <utility>

namespace tatra::desktop {
namespace {

constexpr std::string_view kEllipsoidReference = "WGS84 ellipsoid";

struct DatasetCloser {
    void operator()(GDALDataset* dataset) const {
        if (dataset)
            GDALClose(dataset);
    }
};
using DatasetPtr = std::unique_ptr<GDALDataset, DatasetCloser>;

Error invalid(std::string message) {
    return {ErrorCode::InvalidInput, std::move(message)};
}

Error corrupt(std::string message) {
    return {ErrorCode::CorruptData, std::move(message)};
}

Error ioError(std::string message) {
    const char* detail = CPLGetLastErrorMsg();
    if (detail && *detail)
        message += ": " + std::string{detail};
    return {ErrorCode::IoFailure, std::move(message)};
}

bool finiteTransform(const std::array<double, 6>& transform) {
    return std::all_of(transform.begin(), transform.end(),
                       [](double value) { return std::isfinite(value); });
}

Result<std::monostate> validateRasterShape(const DemGeoTiff& raster) {
    if (raster.geometry.rows < 2 || raster.geometry.columns < 2)
        return invalid("DEM must contain at least two rows and two columns");
    if (raster.geometry.rows >
        std::numeric_limits<std::size_t>::max() / raster.geometry.columns)
        return invalid("DEM dimensions overflow size_t");
    const auto count = raster.geometry.rows * raster.geometry.columns;
    if (raster.height_values_m.size() != count || raster.valid_mask.size() != count)
        return invalid("DEM values and mask must match its dimensions");
    if (!finiteTransform(raster.geo_transform))
        return invalid("DEM geotransform must be finite");
    if (raster.geo_transform[1] <= 0.0 || raster.geo_transform[5] >= 0.0 ||
        raster.geo_transform[2] != 0.0 || raster.geo_transform[4] != 0.0)
        return invalid("DEM must be a north-up grid with positive longitude spacing");
    for (std::size_t index = 0; index < count; ++index) {
        if (raster.valid_mask[index] != 0 &&
            !std::isfinite(static_cast<double>(raster.height_values_m[index])))
            return invalid("Every valid DEM height must be finite");
    }
    return std::monostate{};
}

bool isWgs84(const OGRSpatialReference* reference) {
    if (!reference)
        return false;
    OGRSpatialReference expected;
    expected.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if (expected.importFromEPSG(4326) != OGRERR_NONE)
        return false;
    auto candidate = reference->Clone();
    if (!candidate)
        return false;
    candidate->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    const bool same = candidate->IsSameGeogCS(&expected) != 0 && candidate->IsGeographic() != 0;
    candidate->Release();
    return same;
}

std::string metadata(GDALDataset& dataset, const char* key) {
    const char* value = dataset.GetMetadataItem(key);
    return value ? value : "";
}

std::vector<std::string> splitMetadata(const std::string& value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find(';', begin);
        result.push_back(value.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return result;
}

void setMetadata(GDALDataset& dataset, const char* key, const std::string& value) {
    if (!value.empty())
        dataset.SetMetadataItem(key, value.c_str());
}

} // namespace

Result<DemGeoTiff> readDemGeoTiff(const std::filesystem::path& file) {
    GDALAllRegister();
    DatasetPtr dataset{static_cast<GDALDataset*>(
        GDALOpenEx(file.string().c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr,
                   nullptr))};
    if (!dataset)
        return ioError("Cannot open DEM GeoTIFF " + file.string());
    if (dataset->GetRasterCount() != 1)
        return invalid("DEM GeoTIFF must contain exactly one raster band");
    if (!isWgs84(dataset->GetSpatialRef()))
        return Error{ErrorCode::UnsupportedFormat,
                     "DEM horizontal CRS must be geographic WGS84 (EPSG:4326)"};

    DemGeoTiff raster;
    raster.geometry.rows = static_cast<std::size_t>(dataset->GetRasterYSize());
    raster.geometry.columns = static_cast<std::size_t>(dataset->GetRasterXSize());
    if (dataset->GetGeoTransform(raster.geo_transform.data()) != CE_None)
        return Error{ErrorCode::UnsupportedFormat, "DEM has no usable geotransform"};
    if (!finiteTransform(raster.geo_transform) || raster.geo_transform[1] <= 0.0 ||
        raster.geo_transform[5] >= 0.0 || raster.geo_transform[2] != 0.0 ||
        raster.geo_transform[4] != 0.0)
        return Error{ErrorCode::UnsupportedFormat,
                     "DEM must be an unrotated north-up raster"};
    raster.geometry.first_center_longitude_deg =
        raster.geo_transform[0] + 0.5 * raster.geo_transform[1] +
        0.5 * raster.geo_transform[2];
    raster.geometry.first_center_latitude_deg =
        raster.geo_transform[3] + 0.5 * raster.geo_transform[4] +
        0.5 * raster.geo_transform[5];
    raster.geometry.longitude_step_deg = raster.geo_transform[1];
    raster.geometry.latitude_step_deg = raster.geo_transform[5];
    raster.projection_wkt = dataset->GetProjectionRef() ? dataset->GetProjectionRef() : "";
    raster.area_or_point = metadata(*dataset, "AREA_OR_POINT");
    raster.vertical_reference = metadata(*dataset, "TATRA_VERTICAL_REFERENCE");
    raster.source_vertical_reference =
        metadata(*dataset, "TATRA_SOURCE_VERTICAL_REFERENCE");
    raster.geoid_model = metadata(*dataset, "TATRA_GEOID_MODEL");
    raster.geoid_sha256 = metadata(*dataset, "TATRA_GEOID_SHA256");
    raster.normalization_formula = metadata(*dataset, "TATRA_NORMALIZATION_FORMULA");
    raster.source_files =
        splitMetadata(metadata(*dataset, "TATRA_SOURCE_DEM_FILES"));
    raster.source_sha256 =
        splitMetadata(metadata(*dataset, "TATRA_SOURCE_DEM_SHA256"));

    if (raster.geometry.rows >
        std::numeric_limits<std::size_t>::max() / raster.geometry.columns)
        return corrupt("DEM dimensions overflow size_t");
    const std::size_t count = raster.geometry.rows * raster.geometry.columns;
    raster.height_values_m.resize(count);
    raster.valid_mask.resize(count, std::uint8_t{255});
    auto* band = dataset->GetRasterBand(1);
    if (!band)
        return corrupt("DEM has no first raster band");
    int has_no_data = 0;
    const double no_data = band->GetNoDataValue(&has_no_data);
    if (has_no_data)
        raster.no_data_value = no_data;
    if (band->RasterIO(GF_Read, 0, 0, dataset->GetRasterXSize(), dataset->GetRasterYSize(),
                       raster.height_values_m.data(), dataset->GetRasterXSize(),
                       dataset->GetRasterYSize(), GDT_Float32, 0, 0, nullptr) != CE_None)
        return ioError("Cannot read DEM height band");
    if ((band->GetMaskFlags() & GMF_ALL_VALID) == 0) {
        auto* mask = band->GetMaskBand();
        if (!mask ||
            mask->RasterIO(GF_Read, 0, 0, dataset->GetRasterXSize(),
                           dataset->GetRasterYSize(), raster.valid_mask.data(),
                           dataset->GetRasterXSize(), dataset->GetRasterYSize(), GDT_Byte, 0, 0,
                           nullptr) != CE_None)
            return ioError("Cannot read DEM validity mask");
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::isfinite(static_cast<double>(raster.height_values_m[index])))
            raster.valid_mask[index] = 0;
    }
    return raster;
}

Result<GeoCoord> pixelCenterGeoCoord(const DemGeoTiff& raster, std::size_t row,
                                     std::size_t column) {
    if (row >= raster.geometry.rows || column >= raster.geometry.columns)
        return invalid("DEM pixel index is outside the raster");
    if (!finiteTransform(raster.geo_transform))
        return invalid("DEM geotransform must be finite");
    const double pixel_x = static_cast<double>(column) + 0.5;
    const double pixel_y = static_cast<double>(row) + 0.5;
    const double longitude = raster.geo_transform[0] + pixel_x * raster.geo_transform[1] +
                             pixel_y * raster.geo_transform[2];
    const double latitude = raster.geo_transform[3] + pixel_x * raster.geo_transform[4] +
                            pixel_y * raster.geo_transform[5];
    if (!std::isfinite(latitude) || !std::isfinite(longitude))
        return invalid("DEM pixel centre is not finite");
    return GeoCoord{latitude, longitude};
}

Result<DemGeoTiff> normalizeHeightGridToEllipsoid(const DemGeoTiff& raster,
                                                   const GeoidHeightProvider& geoid_height,
                                                   std::string geoid_model,
                                                   std::string geoid_sha256) {
    const auto valid = validateRasterShape(raster);
    if (const auto* error = std::get_if<Error>(&valid))
        return *error;
    if (!geoid_height)
        return invalid("A geoid height provider is required");
    if (raster.vertical_reference.empty())
        return invalid("Source vertical reference must be explicit before normalization");
    if (geoid_model.empty() || geoid_sha256.empty())
        return invalid("Geoid model and SHA-256 must be recorded");

    DemGeoTiff normalized = raster;
    for (std::size_t row = 0; row < raster.geometry.rows; ++row) {
        for (std::size_t column = 0; column < raster.geometry.columns; ++column) {
            const std::size_t index = row * raster.geometry.columns + column;
            if (raster.valid_mask[index] == 0)
                continue;
            const auto position = pixelCenterGeoCoord(raster, row, column);
            if (const auto* error = std::get_if<Error>(&position))
                return *error;
            const auto undulation = geoid_height(std::get<GeoCoord>(position));
            if (const auto* error = std::get_if<Error>(&undulation))
                return *error;
            const double geoid_height_m = std::get<double>(undulation);
            const double ellipsoid_height_m =
                static_cast<double>(raster.height_values_m[index]) + geoid_height_m;
            if (!std::isfinite(geoid_height_m) || !std::isfinite(ellipsoid_height_m) ||
                ellipsoid_height_m < -static_cast<double>(std::numeric_limits<float>::max()) ||
                ellipsoid_height_m > static_cast<double>(std::numeric_limits<float>::max()))
                return invalid("Geoid normalization produced a non-finite height");
            normalized.height_values_m[index] = static_cast<float>(ellipsoid_height_m);
        }
    }
    normalized.source_vertical_reference = raster.vertical_reference;
    normalized.vertical_reference = std::string{kEllipsoidReference};
    normalized.geoid_model = std::move(geoid_model);
    normalized.geoid_sha256 = std::move(geoid_sha256);
    normalized.normalization_formula = "h = H + N";
    return normalized;
}

Status writeNormalizedGeoTiff(const DemGeoTiff& raster,
                              const std::filesystem::path& output) {
    const auto valid = validateRasterShape(raster);
    if (const auto* error = std::get_if<Error>(&valid))
        return *error;
    if (raster.vertical_reference != kEllipsoidReference || raster.geoid_model.empty() ||
        raster.geoid_sha256.empty() || raster.normalization_formula != "h = H + N")
        return invalid("Normalized DEM is missing vertical conversion metadata");
    if (raster.geometry.columns > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        raster.geometry.rows > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return invalid("DEM dimensions exceed the GDAL integer range");

    GDALAllRegister();
    auto* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver)
        return Error{ErrorCode::UnsupportedFormat, "GDAL GTiff driver is unavailable"};
    char option_tiled[] = "TILED=YES";
    char option_compress[] = "COMPRESS=DEFLATE";
    char option_predictor[] = "PREDICTOR=3";
    char* creation_options[]{option_tiled, option_compress, option_predictor, nullptr};
    DatasetPtr dataset{driver->Create(output.string().c_str(),
                                      static_cast<int>(raster.geometry.columns),
                                      static_cast<int>(raster.geometry.rows), 1, GDT_Float32,
                                      creation_options)};
    if (!dataset)
        return ioError("Cannot create normalized DEM " + output.string());
    auto transform = raster.geo_transform;
    if (dataset->SetGeoTransform(transform.data()) != CE_None ||
        dataset->SetProjection(raster.projection_wkt.c_str()) != CE_None)
        return ioError("Cannot write normalized DEM spatial metadata");
    dataset->SetMetadataItem("AREA_OR_POINT", "Point");
    setMetadata(*dataset, "TATRA_VERTICAL_REFERENCE", raster.vertical_reference);
    setMetadata(*dataset, "TATRA_SOURCE_VERTICAL_REFERENCE",
                raster.source_vertical_reference);
    setMetadata(*dataset, "TATRA_GEOID_MODEL", raster.geoid_model);
    setMetadata(*dataset, "TATRA_GEOID_SHA256", raster.geoid_sha256);
    setMetadata(*dataset, "TATRA_NORMALIZATION_FORMULA", raster.normalization_formula);
    if (!raster.source_files.empty()) {
        std::string files;
        std::string hashes;
        for (std::size_t index = 0; index < raster.source_files.size(); ++index) {
            if (index != 0) {
                files += ';';
                hashes += ';';
            }
            files += raster.source_files[index];
            if (index < raster.source_sha256.size())
                hashes += raster.source_sha256[index];
        }
        setMetadata(*dataset, "TATRA_SOURCE_DEM_FILES", files);
        setMetadata(*dataset, "TATRA_SOURCE_DEM_SHA256", hashes);
    }

    auto* band = dataset->GetRasterBand(1);
    if (!band)
        return ioError("Cannot access normalized DEM raster band");
    if (raster.no_data_value && band->SetNoDataValue(*raster.no_data_value) != CE_None)
        return ioError("Cannot write normalized DEM NoData value");
    if (band->RasterIO(GF_Write, 0, 0, static_cast<int>(raster.geometry.columns),
                       static_cast<int>(raster.geometry.rows),
                       const_cast<float*>(raster.height_values_m.data()),
                       static_cast<int>(raster.geometry.columns),
                       static_cast<int>(raster.geometry.rows), GDT_Float32, 0, 0,
                       nullptr) != CE_None)
        return ioError("Cannot write normalized DEM height band");
    CPLSetThreadLocalConfigOption("GDAL_TIFF_INTERNAL_MASK", "YES");
    const auto mask_status = band->CreateMaskBand(GMF_PER_DATASET);
    CPLSetThreadLocalConfigOption("GDAL_TIFF_INTERNAL_MASK", nullptr);
    if (mask_status != CE_None)
        return ioError("Cannot create normalized DEM validity mask");
    auto* mask = band->GetMaskBand();
    if (!mask ||
        mask->RasterIO(GF_Write, 0, 0, static_cast<int>(raster.geometry.columns),
                       static_cast<int>(raster.geometry.rows),
                       const_cast<std::uint8_t*>(raster.valid_mask.data()),
                       static_cast<int>(raster.geometry.columns),
                       static_cast<int>(raster.geometry.rows), GDT_Byte, 0, 0,
                       nullptr) != CE_None)
        return ioError("Cannot write normalized DEM validity mask");
    if (dataset->FlushCache() != CE_None)
        return ioError("Cannot flush normalized DEM");
    dataset.reset();
    return std::monostate{};
}

Result<GridTerrain> loadNormalizedGeoTiff(const std::filesystem::path& file) {
    auto loaded = readDemGeoTiff(file);
    if (const auto* error = std::get_if<Error>(&loaded))
        return *error;
    auto raster = std::get<DemGeoTiff>(std::move(loaded));
    if (raster.vertical_reference != kEllipsoidReference || raster.geoid_model.empty() ||
        raster.geoid_sha256.empty() || raster.normalization_formula != "h = H + N")
        return Error{ErrorCode::UnsupportedFormat,
                     "DEM is not explicitly normalized to WGS84 ellipsoid heights"};
    return GridTerrain::create(raster.geometry, std::move(raster.height_values_m),
                               std::move(raster.valid_mask));
}

} // namespace tatra::desktop
