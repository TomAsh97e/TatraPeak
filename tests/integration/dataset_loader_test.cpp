#include "file_hash.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gdal_priv.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <ogr_spatialref.h>
#include <tatra/desktop/io.hpp>
#include <vector>

namespace {
using namespace tatra;
using namespace tatra::desktop;
using Json = nlohmann::json;

class DatasetLoader : public testing::Test {
  protected:
    std::filesystem::path directory;
    DatasetManifestOptions manifest_options;

    void SetUp() override {
        GDALAllRegister();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() / ("tatra-t13-" + std::to_string(stamp));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        createTerrain();
        createPeaks();
        const auto source_metadata = writeJson(
            "download-metadata.json", {{"schema_version", 1},
                                       {"retrieved_at", "2026-09-25T21:16:22Z"},
                                       {"dem_working_bbox_wsen", {19.5, 48.5, 21.5, 50.5}},
                                       {"osm_query_bbox_swne", {49.0, 20.0, 50.0, 21.0}},
                                       {"sources",
                                        {{{"path", "raw/raw-dem.tif"},
                                          {"source", "https://example.invalid/raw-dem.tif"},
                                          {"dataset", "Synthetic DEM fixture"},
                                          {"sha256", std::string(64, 'a')}}}}});
        const auto attribution = directory / "source-attribution.txt";
        writeText(attribution, "Copernicus DEM GLO-30 Public\n"
                               "GeographicLib geoid dataset egm2008-2_5\n"
                               "OpenStreetMap contributors\n");
        manifest_options = {"fixture-v1",
                            directory / "terrain-ellipsoid.tif",
                            directory / "peaks.json",
                            source_metadata,
                            attribution,
                            directory};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(writeDatasetManifest(manifest_options)));
    }

    void TearDown() override { std::filesystem::remove_all(directory); }

    static void writeText(const std::filesystem::path& path, const std::string& contents) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << contents;
        ASSERT_TRUE(output.good());
    }

    std::filesystem::path writeJson(const char* name, const Json& document) const {
        const auto path = directory / name;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << document.dump(2) << '\n';
        EXPECT_TRUE(output.good());
        return path;
    }

    Json readJson(const std::filesystem::path& path) const {
        std::ifstream input(path, std::ios::binary);
        EXPECT_TRUE(input.good());
        return Json::parse(input);
    }

    void createTerrain() {
        OGRSpatialReference reference;
        ASSERT_EQ(reference.importFromEPSG(4326), OGRERR_NONE);
        reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        char* wkt = nullptr;
        ASSERT_EQ(reference.exportToWkt(&wkt), OGRERR_NONE);
        DemGeoTiff raster;
        raster.geometry = {2, 2, 50.0, 20.0, -1.0, 1.0};
        raster.geo_transform = {19.5, 1.0, 0.0, 50.5, 0.0, -1.0};
        raster.height_values_m = {1234.0F, 1234.0F, 1234.0F, 1234.0F};
        raster.valid_mask = {255, 255, 255, 255};
        raster.projection_wkt = wkt;
        CPLFree(wkt);
        raster.area_or_point = "Point";
        raster.vertical_reference = "WGS84 ellipsoid";
        raster.source_vertical_reference = "EGM2008 (EPSG:3855)";
        raster.geoid_model = "egm2008-2_5";
        raster.geoid_sha256 = std::string(64, 'b');
        raster.normalization_formula = "h = H + N";
        raster.source_files = {"raw-dem.tif"};
        raster.source_sha256 = {std::string(64, 'a')};
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            writeNormalizedGeoTiff(raster, directory / "terrain-ellipsoid.tif")));
    }

    void createPeaks() {
        Json elements = Json::array();
        Json ids = Json::array();
        for (std::uint64_t id = 1; id <= 30; ++id) {
            elements.push_back({{"type", "node"},
                                {"id", id},
                                {"lat", 49.75},
                                {"lon", 20.25},
                                {"tags",
                                 {{"natural", "peak"},
                                  {"name", "Fixture peak " + std::to_string(id)},
                                  {"ele", std::to_string(2000 + id)}}}});
            ids.push_back("osm:node:" + std::to_string(id));
        }
        const auto raw_osm = writeJson("raw-osm.json", {{"elements", elements}});
        const auto selection =
            writeJson("selection.json", {{"schema_version", 1}, {"osm_ids", ids}});
        const auto corrections =
            writeJson("corrections.json", {{"schema_version", 1}, {"corrections", Json::array()}});
        auto prepared = preparePeakCatalog(
            {raw_osm, directory / "terrain-ellipsoid.tif", selection, corrections, directory});
        ASSERT_TRUE(std::holds_alternative<PeakImportReport>(prepared));
        ASSERT_EQ(std::get<PeakImportReport>(prepared).accepted_count, 30U);
    }

    void rewriteManifest(const std::function<void(Json&)>& change) {
        const auto path = directory / "manifest.json";
        auto manifest = readJson(path);
        change(manifest);
        writeText(path, manifest.dump(2) + '\n');
    }

    void refreshManifestFile(const char* key, const std::filesystem::path& path) {
        const auto hash = detail::sha256File(path);
        ASSERT_TRUE(std::holds_alternative<std::string>(hash));
        rewriteManifest([&](Json& manifest) {
            manifest["files"][key]["sha256"] = std::get<std::string>(hash);
            manifest["files"][key]["bytes"] = std::filesystem::file_size(path);
        });
    }

    Error loadError() const {
        auto loaded = loadDataset(directory);
        EXPECT_TRUE(std::holds_alternative<Error>(loaded));
        return std::get<Error>(std::move(loaded));
    }
};

TEST_F(DatasetLoader, MatchingPackageCreatesOwnedDataset) {
    auto result = loadDataset(directory);
    ASSERT_TRUE(std::holds_alternative<Dataset>(result));
    const auto& dataset = std::get<Dataset>(result);
    EXPECT_EQ(dataset.metadata.dataset_id, "fixture-v1");
    EXPECT_EQ(dataset.metadata.horizontal_reference, "EPSG:4326");
    EXPECT_EQ(dataset.metadata.vertical_reference, "WGS84 ellipsoid");
    EXPECT_EQ(dataset.metadata.terrain_rows, 2U);
    EXPECT_EQ(dataset.metadata.terrain_columns, 2U);
    EXPECT_EQ(dataset.metadata.peak_count, 30U);
    EXPECT_EQ(dataset.catalog.peaks.size(), 30U);
    EXPECT_EQ(dataset.metadata.manifest_sha256.size(), 64U);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(validateDataset(dataset)));
}

TEST_F(DatasetLoader, MobilePackageHasApprovedHeaderLengthsAndSelfHash) {
    const auto package = directory / "fixture.tpk";
    ASSERT_TRUE(std::holds_alternative<std::monostate>(writeMobilePackage(directory, package)));
    std::ifstream input(package, std::ios::binary);
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
    ASSERT_GE(bytes.size(), 168U);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes.data()), 7), "TATRAPK");
    EXPECT_EQ(bytes[8], 1);
    const std::vector<std::uint8_t> expected(bytes.begin() + 104, bytes.begin() + 136);
    std::fill(bytes.begin() + 104, bytes.begin() + 136, 0);
    const auto unhashed = directory / "fixture-unhashed.tpk";
    std::ofstream output(unhashed, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    auto hash = detail::sha256File(unhashed);
    ASSERT_TRUE(std::holds_alternative<std::string>(hash));
    std::string encoded;
    constexpr char hex[] = "0123456789abcdef";
    for (const auto byte : expected) { encoded += hex[byte >> 4U]; encoded += hex[byte & 15U]; }
    EXPECT_EQ(encoded, std::get<std::string>(hash));

    rewriteManifest([](Json& manifest) { manifest["files"]["terrain"]["sha256"] = std::string(64, '0'); });
    EXPECT_TRUE(std::holds_alternative<Error>(writeMobilePackage(directory, directory / "bad.tpk")));
}

TEST_F(DatasetLoader, ExplicitHeightRoundTripsWithoutChangingDemOrDisplayHeight) {
    const auto corrections =
        writeJson("corrections.json",
                  {{"schema_version", 1},
                   {"corrections",
                    {{{"id", "osm:node:1"},
                      {"note", "Synthetic height correction"},
                      {"geometry_height_correction",
                       {{"ellipsoid_height_m", 2100.0}, {"source", "Survey fixture"}}}}}}});
    ASSERT_TRUE(std::holds_alternative<PeakImportReport>(
        preparePeakCatalog({directory / "raw-osm.json", directory / "terrain-ellipsoid.tif",
                            directory / "selection.json", corrections, directory})));
    ASSERT_TRUE(std::holds_alternative<std::monostate>(writeDatasetManifest(manifest_options)));
    EXPECT_EQ(readJson(directory / "manifest.json")["catalog"]["geometry_height_source"], "Mixed");
    auto loaded = loadDataset(directory);
    ASSERT_TRUE(std::holds_alternative<Dataset>(loaded));
    const auto& dataset = std::get<Dataset>(loaded);
    const auto& peak = dataset.catalog.peaks[0];
    EXPECT_EQ(peak.geometry_height_source, GeometryHeightSource::ExplicitEllipsoid);
    EXPECT_DOUBLE_EQ(peak.geometry_position.ellipsoid_height_m, 2100.0);
    ASSERT_TRUE(peak.display_elevation);
    EXPECT_DOUBLE_EQ(peak.display_elevation->metres, 2001.0);
    EXPECT_DOUBLE_EQ(
        std::get<HeightSample>(dataset.terrain.sampleHeight(peak.geometry_position.coord))
            .ellipsoid_height_m,
        1234.0);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(validateDataset(dataset)));

    const auto original = readJson(directory / "peaks.json");
    for (int failure = 0; failure < 5; ++failure) {
        auto peaks = original;
        auto& record = peaks["peaks"][0];
        if (failure == 0)
            record["geometry_height_correction"]["source"] = "";
        if (failure == 1)
            record["correction_note"] = nullptr;
        if (failure == 2)
            record["geometry_position"]["ellipsoid_height_m"] = 2110.0;
        if (failure == 3)
            record.erase("geometry_height_correction");
        if (failure == 4)
            record["geometry_position"]["latitude_deg"] = 60.0;
        writeJson("peaks.json", peaks);
        EXPECT_TRUE(std::holds_alternative<Error>(writeDatasetManifest(manifest_options)));
    }
    writeJson("peaks.json", original);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(writeDatasetManifest(manifest_options)));
    rewriteManifest(
        [](Json& manifest) { manifest["catalog"]["geometry_height_source"] = "DemSample"; });
    EXPECT_EQ(loadError().code, ErrorCode::CorruptData);
}

TEST_F(DatasetLoader, DemHeightMismatchStillRejected) {
    auto peaks = readJson(directory / "peaks.json");
    peaks["peaks"][0]["geometry_position"]["ellipsoid_height_m"] = 2100.0;
    writeJson("peaks.json", peaks);
    EXPECT_TRUE(std::holds_alternative<Error>(writeDatasetManifest(manifest_options)));
}

TEST_F(DatasetLoader, RejectsUnsupportedVersionMissingFileAndBadHash) {
    rewriteManifest([](Json& manifest) { manifest["schema_version"] = 2; });
    EXPECT_EQ(loadError().code, ErrorCode::UnsupportedFormat);

    ASSERT_TRUE(std::holds_alternative<std::monostate>(writeDatasetManifest(manifest_options)));
    std::filesystem::rename(directory / "peaks.json", directory / "missing-peaks.json");
    EXPECT_EQ(loadError().code, ErrorCode::IoFailure);
    std::filesystem::rename(directory / "missing-peaks.json", directory / "peaks.json");

    rewriteManifest(
        [](Json& manifest) { manifest["files"]["terrain"]["sha256"] = std::string(64, '0'); });
    EXPECT_EQ(loadError().code, ErrorCode::CorruptData);
}

TEST_F(DatasetLoader, RejectsManifestDimensionsAndGeotransformMismatch) {
    rewriteManifest([](Json& manifest) { manifest["raster"]["rows"] = 3; });
    EXPECT_EQ(loadError().code, ErrorCode::CorruptData);

    ASSERT_TRUE(std::holds_alternative<std::monostate>(writeDatasetManifest(manifest_options)));
    rewriteManifest([](Json& manifest) {
        manifest["raster"]["geo_transform"][0] =
            manifest["raster"]["geo_transform"][0].get<double>() + 0.25;
    });
    EXPECT_EQ(loadError().code, ErrorCode::CorruptData);
}

TEST_F(DatasetLoader, RejectsUnsupportedHorizontalCrs) {
    auto* dataset = static_cast<GDALDataset*>(
        GDALOpen((directory / "terrain-ellipsoid.tif").string().c_str(), GA_Update));
    ASSERT_NE(dataset, nullptr);
    OGRSpatialReference reference;
    ASSERT_EQ(reference.importFromEPSG(3857), OGRERR_NONE);
    reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    char* wkt = nullptr;
    ASSERT_EQ(reference.exportToWkt(&wkt), OGRERR_NONE);
    ASSERT_EQ(dataset->SetProjection(wkt), CE_None);
    CPLFree(wkt);
    GDALClose(dataset);
    refreshManifestFile("terrain", directory / "terrain-ellipsoid.tif");
    EXPECT_EQ(loadError().code, ErrorCode::UnsupportedFormat);
}

TEST_F(DatasetLoader, RejectsIncompatibleHeightReference) {
    rewriteManifest([](Json& manifest) {
        manifest["spatial_reference"]["result_vertical"] = "EGM2008 (EPSG:3855)";
    });
    EXPECT_EQ(loadError().code, ErrorCode::UnsupportedFormat);
}

TEST_F(DatasetLoader, EllipsoidFilenameAndLabelDoNotReplaceConversionProvenance) {
    auto* dataset = static_cast<GDALDataset*>(
        GDALOpen((directory / "terrain-ellipsoid.tif").string().c_str(), GA_Update));
    ASSERT_NE(dataset, nullptr);
    ASSERT_EQ(dataset->SetMetadataItem("TATRA_GEOID_SHA256", nullptr), CE_None);
    ASSERT_EQ(dataset->SetMetadataItem("TATRA_NORMALIZATION_FORMULA", nullptr), CE_None);
    GDALClose(dataset);
    refreshManifestFile("terrain", directory / "terrain-ellipsoid.tif");
    EXPECT_EQ(loadError().code, ErrorCode::UnsupportedFormat);
}

TEST_F(DatasetLoader, RejectsDuplicatePeakIds) {
    auto peaks = readJson(directory / "peaks.json");
    peaks["peaks"][1]["id"] = peaks["peaks"][0]["id"];
    peaks["peaks"][1]["source"]["osm_id"] = peaks["peaks"][0]["source"]["osm_id"];
    writeText(directory / "peaks.json", peaks.dump(2) + '\n');
    EXPECT_TRUE(std::holds_alternative<Error>(writeDatasetManifest(manifest_options)));
    EXPECT_EQ(loadError().code, ErrorCode::CorruptData);
}

TEST_F(DatasetLoader, RejectsPeakOutsideTerrainCoverage) {
    auto peaks = readJson(directory / "peaks.json");
    peaks["peaks"][0]["geometry_position"]["latitude_deg"] = 60.0;
    writeText(directory / "peaks.json", peaks.dump(2) + '\n');
    EXPECT_TRUE(std::holds_alternative<Error>(writeDatasetManifest(manifest_options)));
    EXPECT_EQ(loadError().code, ErrorCode::CorruptData);
}
} // namespace
