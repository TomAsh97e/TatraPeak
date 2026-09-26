#include <chrono>
#include <cpl_conv.h>
#include <cpl_string.h>
#include <filesystem>
#include <gdal_priv.h>
#include <gtest/gtest.h>
#include <ogr_spatialref.h>
#include <tatra/desktop/io.hpp>
#include <vector>

namespace {
using namespace tatra;
using namespace tatra::desktop;

class GeoTiff : public testing::Test {
  protected:
    std::filesystem::path directory;

    void SetUp() override {
        GDALAllRegister();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory =
            std::filesystem::temp_directory_path() / ("tatra-t11-" + std::to_string(stamp));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
    }

    void TearDown() override { std::filesystem::remove_all(directory); }

    std::filesystem::path createRaster(const char* name, int epsg = 4326,
                                       bool rotated = false) {
        const auto path = directory / name;
        auto* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        EXPECT_NE(driver, nullptr);
        auto* dataset = driver->Create(path.string().c_str(), 3, 2, 1, GDT_Float32, nullptr);
        EXPECT_NE(dataset, nullptr);
        if (!dataset)
            return path;
        double transform[6]{20.0, 0.1, rotated ? 0.01 : 0.0, 50.0, 0.0, -0.2};
        EXPECT_EQ(dataset->SetGeoTransform(transform), CE_None);
        OGRSpatialReference reference;
        EXPECT_EQ(reference.importFromEPSG(epsg), OGRERR_NONE);
        reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        char* wkt = nullptr;
        EXPECT_EQ(reference.exportToWkt(&wkt), OGRERR_NONE);
        EXPECT_EQ(dataset->SetProjection(wkt), CE_None);
        CPLFree(wkt);
        dataset->SetMetadataItem("AREA_OR_POINT", "Point");

        std::vector<float> values{1000.0F, 0.0F, 1002.0F, 2000.0F, 2001.0F, 2002.0F};
        std::vector<std::uint8_t> mask{255, 255, 0, 255, 255, 255};
        auto* band = dataset->GetRasterBand(1);
        EXPECT_EQ(band->SetNoDataValue(-9999.0), CE_None);
        EXPECT_EQ(band->RasterIO(GF_Write, 0, 0, 3, 2, values.data(), 3, 2, GDT_Float32,
                                 0, 0, nullptr),
                  CE_None);
        CPLSetThreadLocalConfigOption("GDAL_TIFF_INTERNAL_MASK", "YES");
        EXPECT_EQ(band->CreateMaskBand(GMF_PER_DATASET), CE_None);
        CPLSetThreadLocalConfigOption("GDAL_TIFF_INTERNAL_MASK", nullptr);
        EXPECT_EQ(band->GetMaskBand()->RasterIO(GF_Write, 0, 0, 3, 2, mask.data(), 3, 2,
                                                GDT_Byte, 0, 0, nullptr),
                  CE_None);
        GDALClose(dataset);
        return path;
    }

    DemGeoTiff read(const std::filesystem::path& path) {
        auto result = readDemGeoTiff(path);
        EXPECT_TRUE(std::holds_alternative<DemGeoTiff>(result));
        return std::get<DemGeoTiff>(std::move(result));
    }
};

TEST_F(GeoTiff, ReadsPixelCentresMaskAndValidZero) {
    const auto raster = read(createRaster("input.tif"));
    EXPECT_EQ(raster.geometry.rows, 2U);
    EXPECT_EQ(raster.geometry.columns, 3U);
    EXPECT_DOUBLE_EQ(raster.geometry.first_center_longitude_deg, 20.05);
    EXPECT_DOUBLE_EQ(raster.geometry.first_center_latitude_deg, 49.9);
    EXPECT_FLOAT_EQ(raster.height_values_m[1], 0.0F);
    EXPECT_NE(raster.valid_mask[1], 0);
    EXPECT_EQ(raster.valid_mask[2], 0);
    ASSERT_TRUE(raster.no_data_value);
    EXPECT_DOUBLE_EQ(*raster.no_data_value, -9999.0);

    const auto first = pixelCenterGeoCoord(raster, 0, 0);
    ASSERT_TRUE(std::holds_alternative<GeoCoord>(first));
    EXPECT_DOUBLE_EQ(std::get<GeoCoord>(first).latitude_deg, 49.9);
    EXPECT_DOUBLE_EQ(std::get<GeoCoord>(first).longitude_deg, 20.05);
    const auto last = pixelCenterGeoCoord(raster, 1, 2);
    ASSERT_TRUE(std::holds_alternative<GeoCoord>(last));
    EXPECT_DOUBLE_EQ(std::get<GeoCoord>(last).latitude_deg, 49.7);
    EXPECT_DOUBLE_EQ(std::get<GeoCoord>(last).longitude_deg, 20.25);
    EXPECT_TRUE(std::holds_alternative<Error>(pixelCenterGeoCoord(raster, 2, 0)));
}

TEST_F(GeoTiff, ConstantGeoidAddsFortyMetresOnlyToValidPixels) {
    auto raster = read(createRaster("input.tif"));
    raster.vertical_reference = "EGM2008 (EPSG:3855)";
    std::size_t calls = 0;
    const GeoidHeightProvider constant = [&calls](GeoCoord) -> Result<double> {
        ++calls;
        return 40.0;
    };
    const auto result = normalizeHeightGridToEllipsoid(raster, constant, "synthetic-40m",
                                                        "fixture-sha256");
    ASSERT_TRUE(std::holds_alternative<DemGeoTiff>(result));
    const auto& normalized = std::get<DemGeoTiff>(result);
    EXPECT_EQ(calls, 5U);
    EXPECT_FLOAT_EQ(normalized.height_values_m[0], 1040.0F);
    EXPECT_FLOAT_EQ(normalized.height_values_m[1], 40.0F);
    EXPECT_FLOAT_EQ(normalized.height_values_m[2], 1002.0F);
    EXPECT_FLOAT_EQ(normalized.height_values_m[5], 2042.0F);
    EXPECT_EQ(normalized.valid_mask, raster.valid_mask);
    EXPECT_EQ(normalized.source_vertical_reference, "EGM2008 (EPSG:3855)");
    EXPECT_EQ(normalized.vertical_reference, "WGS84 ellipsoid");
    EXPECT_EQ(normalized.normalization_formula, "h = H + N");
}

TEST_F(GeoTiff, WriteReadRoundTripPreservesRowsColumnsMaskAndMetadata) {
    auto raster = read(createRaster("input.tif"));
    raster.vertical_reference = "EGM2008 (EPSG:3855)";
    const auto normalized = normalizeHeightGridToEllipsoid(
        raster, [](GeoCoord) -> Result<double> { return 40.0; }, "synthetic-40m",
        "fixture-sha256");
    ASSERT_TRUE(std::holds_alternative<DemGeoTiff>(normalized));
    const auto output = directory / "normalized.tif";
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        writeNormalizedGeoTiff(std::get<DemGeoTiff>(normalized), output)));

    const auto round_trip = read(output);
    ASSERT_EQ(round_trip.height_values_m.size(), 6U);
    EXPECT_FLOAT_EQ(round_trip.height_values_m[0], 1040.0F);
    EXPECT_FLOAT_EQ(round_trip.height_values_m[1], 40.0F);
    EXPECT_FLOAT_EQ(round_trip.height_values_m[3], 2040.0F);
    EXPECT_FLOAT_EQ(round_trip.height_values_m[5], 2042.0F);
    EXPECT_EQ(round_trip.valid_mask[2], 0);
    EXPECT_EQ(round_trip.vertical_reference, "WGS84 ellipsoid");
    EXPECT_EQ(round_trip.source_vertical_reference, "EGM2008 (EPSG:3855)");
    EXPECT_EQ(round_trip.geoid_model, "synthetic-40m");
    EXPECT_EQ(round_trip.geoid_sha256, "fixture-sha256");

    const auto loaded = loadNormalizedGeoTiff(output);
    ASSERT_TRUE(std::holds_alternative<GridTerrain>(loaded));
    const auto sample =
        std::get<GridTerrain>(loaded).sampleHeight({49.9, 20.05});
    ASSERT_TRUE(std::holds_alternative<HeightSample>(sample));
    EXPECT_DOUBLE_EQ(std::get<HeightSample>(sample).ellipsoid_height_m, 1040.0);
}

TEST_F(GeoTiff, RejectsUnsupportedCrsRotationAndUnnormalizedOutput) {
    const auto projected = readDemGeoTiff(createRaster("projected.tif", 3857));
    ASSERT_TRUE(std::holds_alternative<Error>(projected));
    EXPECT_EQ(std::get<Error>(projected).code, ErrorCode::UnsupportedFormat);
    const auto rotated = readDemGeoTiff(createRaster("rotated.tif", 4326, true));
    ASSERT_TRUE(std::holds_alternative<Error>(rotated));
    EXPECT_EQ(std::get<Error>(rotated).code, ErrorCode::UnsupportedFormat);

    const auto raw = read(createRaster("raw.tif"));
    const auto status = writeNormalizedGeoTiff(raw, directory / "bad.tif");
    ASSERT_TRUE(std::holds_alternative<Error>(status));
    EXPECT_EQ(std::get<Error>(status).code, ErrorCode::InvalidInput);
}

TEST_F(GeoTiff, MissingGeoidStopsPreparation) {
    PrepareOptions options;
    options.raw_dem = {createRaster("input.tif")};
    options.geoid_directory = directory / "missing-geoid";
    options.output_directory = directory / "output";
    options.bounds = {20.0, 49.6, 20.3, 50.0};
    const auto result = prepareDataset(options);
    ASSERT_TRUE(std::holds_alternative<Error>(result));
    EXPECT_EQ(std::get<Error>(result).code, ErrorCode::IoFailure);
    EXPECT_FALSE(std::filesystem::exists(options.output_directory / "terrain-ellipsoid.tif"));
}

} // namespace
