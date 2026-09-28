#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <nlohmann/json.hpp>
#include <tatra/desktop/io.hpp>
#include <vector>

namespace {
using namespace tatra;
using namespace tatra::desktop;
using Json = nlohmann::json;

class OsmPeakImporter : public testing::Test {
  protected:
    std::filesystem::path directory;

    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() / ("tatra-t12-" + std::to_string(stamp));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
    }

    void TearDown() override { std::filesystem::remove_all(directory); }

    std::filesystem::path write(const char* name, const Json& document) const {
        const auto path = directory / name;
        std::ofstream output(path, std::ios::binary);
        output << document.dump(2) << '\n';
        EXPECT_TRUE(output.good());
        return path;
    }

    static Json node(std::uint64_t id, double latitude, double longitude, Json tags) {
        tags["natural"] = "peak";
        return {{"type", "node"},
                {"id", id},
                {"lat", latitude},
                {"lon", longitude},
                {"tags", std::move(tags)}};
    }

    static GridTerrain flatTerrain() {
        auto result =
            GridTerrain::create({2, 2, 50.0, 20.0, -1.0, 1.0}, {1100.0F, 1100.0F, 1100.0F, 1100.0F},
                                {255, 255, 255, 255});
        EXPECT_TRUE(std::holds_alternative<GridTerrain>(result));
        return std::get<GridTerrain>(std::move(result));
    }
};

TEST_F(OsmPeakImporter, HandlesNamesElevationsDuplicatesAndTerrainRejections) {
    Json elements = Json::array();
    elements.push_back(node(
        1, 49.8, 20.2,
        {{"name", "Svinica"}, {"name:pl", "Świnica"}, {"name:sk", "Svinica"}, {"ele", "2301"}}));
    elements.push_back(node(2, 49.7, 20.3, {{"name", "Kriváň"}}));
    elements.push_back(node(3, 49.6, 20.4, {{"name:sk", "Ľadový štít"}, {"ele", "około 2600"}}));
    elements.push_back(elements[0]);
    elements.push_back(node(4, 47.0, 20.2, {{"name", "Świnica"}, {"ele", "2200"}}));
    elements.push_back(node(5, 49.5, 20.5, {{"name", "Świnica"}, {"ele", "2100"}}));
    elements.push_back(node(6, 49.5, 20.6, {{"ele", "2000"}}));
    const auto osm = write("peaks.json", {{"version", 0.6}, {"elements", elements}});

    auto parsed = parseOsmPeakNodes(osm);
    ASSERT_TRUE(std::holds_alternative<OsmPeakImport>(parsed));
    std::vector<PeakId> selection{"osm:node:1", "osm:node:2", "osm:node:3",
                                  "osm:node:4", "osm:node:5", "osm:node:6"};
    PeakCorrection correction;
    correction.id = "osm:node:2";
    correction.name = "Krywań";
    correction.note = "Polska nazwa do prezentacji";
    auto validated = validateAndDeduplicatePeaks(std::get<OsmPeakImport>(std::move(parsed)),
                                                 selection, {correction});
    ASSERT_TRUE(std::holds_alternative<OsmPeakImport>(validated));
    const auto& import = std::get<OsmPeakImport>(validated);
    ASSERT_EQ(import.peaks.size(), 5U);
    EXPECT_EQ(import.report.duplicate_count, 1U);
    EXPECT_EQ(import.report.missing_ele_count, 1U);
    EXPECT_EQ(import.report.invalid_ele_count, 1U);
    EXPECT_EQ(import.report.rejected_count, 1U);
    EXPECT_EQ(import.peaks[0].selected_name, "Świnica");
    EXPECT_EQ(import.peaks[1].selected_name, "Krywań");
    EXPECT_EQ(import.peaks[2].selected_name, "Ľadový štít");
    EXPECT_FALSE(import.peaks[1].display_elevation);
    EXPECT_FALSE(import.peaks[2].display_elevation);
    EXPECT_EQ(import.peaks[3].selected_name, import.peaks[4].selected_name);
    EXPECT_NE(import.peaks[3].id, import.peaks[4].id);

    auto prepared =
        assignGeometryHeights(std::get<OsmPeakImport>(std::move(validated)), flatTerrain());
    ASSERT_TRUE(std::holds_alternative<PreparedPeakCatalog>(prepared));
    const auto& catalog = std::get<PreparedPeakCatalog>(prepared);
    EXPECT_EQ(catalog.peaks.size(), 4U);
    EXPECT_EQ(catalog.report.accepted_count, 4U);
    EXPECT_EQ(catalog.report.rejected_count, 2U);
    EXPECT_TRUE(
        std::any_of(catalog.report.issues.begin(), catalog.report.issues.end(),
                    [](const PeakImportIssue& issue) { return issue.reason == "invalid_ele"; }));
    EXPECT_TRUE(
        std::any_of(catalog.report.issues.begin(), catalog.report.issues.end(),
                    [](const PeakImportIssue& issue) { return issue.reason == "outside_dem"; }));
    EXPECT_DOUBLE_EQ(catalog.peaks[0].peak.geometry_position.ellipsoid_height_m, 1100.0);
    ASSERT_TRUE(catalog.peaks[0].peak.display_elevation);
    EXPECT_DOUBLE_EQ(catalog.peaks[0].peak.display_elevation->metres, 2301.0);
    EXPECT_EQ(catalog.peaks[0].peak.display_elevation->vertical_reference, "unspecified");
}

TEST_F(OsmPeakImporter, ProductionPipelineWritesSeparateGeometryAndSourceData) {
    Json elements = Json::array();
    Json ids = Json::array();
    for (std::uint64_t id = 1; id <= 30; ++id) {
        elements.push_back(
            node(id, 49.75, 20.25,
                 {{"name", "Peak " + std::to_string(id)}, {"ele", std::to_string(2000 + id)}}));
        ids.push_back("osm:node:" + std::to_string(id));
    }
    const auto osm = write("raw-osm.json", {{"version", 0.6}, {"elements", elements}});
    const auto selection = write("selection.json", {{"schema_version", 1}, {"osm_ids", ids}});
    const auto corrections = write(
        "corrections.json", {{"schema_version", 1},
                             {"corrections",
                              {{{"id", "osm:node:1"},
                                {"name", "Szczyt pierwszy"},
                                {"note", "Sprawdzone tłumaczenie testowe"}},
                               {{"id", "osm:node:2"},
                                {"geometry_height_correction",
                                 {{"ellipsoid_height_m", 2044.5}, {"source", "Synthetic survey"}}},
                                {"note", "Explicit WGS84 fixture height"}}}}});

    DemGeoTiff raster;
    raster.geometry = {2, 2, 50.0, 20.0, -1.0, 1.0};
    raster.geo_transform = {19.5, 1.0, 0.0, 50.5, 0.0, -1.0};
    raster.height_values_m = {1234.0F, 1234.0F, 1234.0F, 1234.0F};
    raster.valid_mask = {255, 255, 255, 255};
    raster.projection_wkt =
        "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433],"
        "AUTHORITY[\"EPSG\",\"4326\"]]";
    raster.vertical_reference = "WGS84 ellipsoid";
    raster.source_vertical_reference = "fixture";
    raster.geoid_model = "fixture-geoid";
    raster.geoid_sha256 = "fixture-sha256";
    raster.normalization_formula = "h = H + N";
    const auto terrain = directory / "terrain.tif";
    ASSERT_TRUE(std::holds_alternative<std::monostate>(writeNormalizedGeoTiff(raster, terrain)));

    const auto output = directory / "output";
    auto result = preparePeakCatalog({osm, terrain, selection, corrections, output});
    ASSERT_TRUE(std::holds_alternative<PeakImportReport>(result));
    EXPECT_EQ(std::get<PeakImportReport>(result).accepted_count, 30U);

    std::ifstream stream(output / "peaks.json", std::ios::binary);
    ASSERT_TRUE(stream.good());
    const auto document = Json::parse(stream);
    EXPECT_EQ(document["schema_version"], 1);
    EXPECT_EQ(document["report"]["accepted_count"], 30);
    ASSERT_EQ(document["peaks"].size(), 30U);
    EXPECT_EQ(document["peaks"][0]["name"], "Szczyt pierwszy");
    EXPECT_DOUBLE_EQ(document["peaks"][0]["geometry_position"]["ellipsoid_height_m"], 1234.0);
    EXPECT_EQ(document["peaks"][0]["display_elevation"]["metres"], 2001);
    EXPECT_EQ(document["peaks"][0]["source"]["ele"], "2001");
    EXPECT_EQ(document["peaks"][0]["correction_note"], "Sprawdzone tłumaczenie testowe");
    EXPECT_EQ(document["source"]["osm_sha256"].get<std::string>().size(), 64U);
    EXPECT_EQ(document["source"]["terrain_sha256"].get<std::string>().size(), 64U);
    EXPECT_DOUBLE_EQ(document["peaks"][1]["geometry_position"]["ellipsoid_height_m"], 2044.5);
    EXPECT_EQ(document["peaks"][1]["geometry_height_source"], "ExplicitEllipsoid");
    EXPECT_EQ(document["peaks"][1]["geometry_height_correction"]["source"], "Synthetic survey");
    EXPECT_EQ(document["peaks"][1]["display_elevation"]["metres"], 2002);

    // Malformed or undocumented overrides must fail before writing a catalog.
    for (const auto& geometry :
         std::vector<Json>{nullptr,
                           {{"ellipsoid_height_m", 2000}},
                           {{"ellipsoid_height_m", "2000"}, {"source", "fixture"}},
                           {{"ellipsoid_height_m", 2000}, {"source", ""}},
                           {{"ellipsoid_height_m", 2000}, {"source", "fixture"}, {"typo", 1}}}) {
        write("corrections.json", {{"schema_version", 1},
                                   {"corrections",
                                    {{{"id", "osm:node:2"},
                                      {"note", "test"},
                                      {"geometry_height_correction", geometry}}}}});
        EXPECT_TRUE(std::holds_alternative<Error>(
            preparePeakCatalog({osm, terrain, selection, corrections, output})));
    }
}

TEST_F(OsmPeakImporter, GeometryOverrideRequiresFiniteHeightSourceNoteAndTerrainCoverage) {
    OsmPeakNode peak;
    peak.id = "osm:node:1";
    peak.coord = {49.5, 20.5};
    peak.source_names.name = "Peak";
    OsmPeakImport input;
    input.peaks.push_back(peak);
    PeakCorrection correction;
    correction.id = peak.id;
    correction.note = "Synthetic reference";
    correction.geometry_height_correction =
        GeometryHeightCorrection{std::numeric_limits<double>::quiet_NaN(), "fixture"};
    EXPECT_TRUE(
        std::holds_alternative<Error>(validateAndDeduplicatePeaks(input, {peak.id}, {correction})));
    correction.geometry_height_correction = GeometryHeightCorrection{2000, ""};
    EXPECT_TRUE(
        std::holds_alternative<Error>(validateAndDeduplicatePeaks(input, {peak.id}, {correction})));
    correction.geometry_height_correction = GeometryHeightCorrection{2000, "fixture"};
    correction.note.clear();
    EXPECT_TRUE(
        std::holds_alternative<Error>(validateAndDeduplicatePeaks(input, {peak.id}, {correction})));
    correction.note = "Synthetic reference";
    input.peaks[0].coord = {47, 20};
    auto validated = validateAndDeduplicatePeaks(input, {peak.id}, {correction});
    ASSERT_TRUE(std::holds_alternative<OsmPeakImport>(validated));
    auto prepared = assignGeometryHeights(std::get<OsmPeakImport>(validated), flatTerrain());
    ASSERT_TRUE(std::holds_alternative<PreparedPeakCatalog>(prepared));
    EXPECT_TRUE(std::get<PreparedPeakCatalog>(prepared).peaks.empty());
    EXPECT_EQ(std::get<PreparedPeakCatalog>(prepared).report.rejected_count, 1U);
}
} // namespace
