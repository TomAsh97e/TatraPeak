#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <tatra/desktop/io.hpp>

namespace {
using namespace tatra;
using namespace tatra::desktop;
using Json = nlohmann::json;

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::string lineContaining(const std::string& text, const std::string& needle) {
    const auto position = text.find(needle);
    if (position == std::string::npos)
        return {};
    const auto begin = text.rfind('\n', position);
    const auto end = text.find('\n', position);
    return text.substr(begin == std::string::npos ? 0 : begin + 1,
                       end == std::string::npos
                           ? std::string::npos
                           : end - (begin == std::string::npos ? 0 : begin + 1));
}

class Terrain final : public ITerrain {
  public:
    HeightResult value = HeightSample{100.0};
    mutable int calls = 0;
    HeightResult sampleHeight(GeoCoord) const override {
        ++calls;
        return value;
    }
};

class DesktopIo : public testing::Test {
  protected:
    std::filesystem::path directory;
    Terrain terrain;
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() / ("tatra-t08-" + std::to_string(stamp));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
    }
    void TearDown() override { std::filesystem::remove_all(directory); }
    Json fixture(const char* name) {
        std::ifstream input(std::filesystem::path{TATRA_FIXTURES_DIR} / name);
        return Json::parse(input);
    }
    std::filesystem::path write(const std::string& text) {
        const auto path = directory / "scene.json";
        std::ofstream file(path);
        file << text;
        return path;
    }
    Result<FrameQuery> load(const Json& document) {
        return loadScene(write(document.dump()), terrain);
    }
    void expectError(const Result<FrameQuery>& result, ErrorCode code = ErrorCode::InvalidInput) {
        ASSERT_TRUE(std::holds_alternative<Error>(result));
        EXPECT_EQ(std::get<Error>(result).code, code);
        EXPECT_FALSE(std::get<Error>(result).message.empty());
    }
};

TEST_F(DesktopIo, HandwrittenSceneAndJsonRoundTrip) {
    const auto result =
        loadScene(std::filesystem::path{TATRA_FIXTURES_DIR} / "scene.json", terrain);
    ASSERT_TRUE(std::holds_alternative<FrameQuery>(result));
    const auto& query = std::get<FrameQuery>(result);
    EXPECT_EQ(query.timestamp.count(), 42);
    EXPECT_DOUBLE_EQ(query.observer.position.coord.latitude_deg, 49.0);
    EXPECT_DOUBLE_EQ(query.observer.position.coord.longitude_deg, 20.0);
    EXPECT_DOUBLE_EQ(query.observer.position.ellipsoid_height_m, 102.0);
    EXPECT_NEAR(query.camera.fx_px, 640.0, 1e-10);
    EXPECT_NEAR(query.camera.fy_px, 640.0, 1e-10);
    EXPECT_DOUBLE_EQ(query.camera.cx_px, 640.0);
    EXPECT_DOUBLE_EQ(query.camera.cy_px, 360.0);
    EXPECT_LT(
        (query.pose.camera_from_enu * Eigen::Vector3d{1, 0, 0} - Eigen::Vector3d{0, 0, 1}).norm(),
        1e-12);
    EXPECT_DOUBLE_EQ(query.options.min_range_m, 10.0);
    EXPECT_DOUBLE_EQ(query.options.max_range_m, 2000.0);
    EXPECT_DOUBLE_EQ(query.options.near_z_m, 0.1);
    EXPECT_DOUBLE_EQ(query.options.visibility.sample_step_m, 5.0);
    EXPECT_DOUBLE_EQ(query.options.visibility.clearance_band_m, 0.25);
    EXPECT_DOUBLE_EQ(query.options.visibility.start_guard_m, 1.0);
    EXPECT_DOUBLE_EQ(query.options.visibility.target_guard_m, 5.0);
    EXPECT_EQ(query.options.visibility.max_samples, 800U);
    const auto round_trip = load(fixture("scene.json"));
    ASSERT_TRUE(std::holds_alternative<FrameQuery>(round_trip));
    EXPECT_DOUBLE_EQ(std::get<FrameQuery>(round_trip).camera.fx_px, query.camera.fx_px);
    EXPECT_DOUBLE_EQ(std::get<FrameQuery>(round_trip).observer.position.ellipsoid_height_m, 102.0);
}

TEST_F(DesktopIo, EllipsoidIntrinsicsAndAbsentOptions) {
    auto document = fixture("scene.json");
    document["observer"]["height"] = {{"mode", "ellipsoid"}, {"metres", -12.0}};
    document["observer"]["simulated"] = true;
    document["camera"] = {{"width_px", 100}, {"height_px", 200}, {"fx_px", 70},
                          {"fy_px", 80},     {"cx_px", -5},      {"cy_px", 220}};
    document.erase("options");
    const auto result = load(document);
    ASSERT_TRUE(std::holds_alternative<FrameQuery>(result));
    const auto& query = std::get<FrameQuery>(result);
    EXPECT_DOUBLE_EQ(query.observer.position.ellipsoid_height_m, -12.0);
    EXPECT_TRUE(query.observer.simulated);
    EXPECT_DOUBLE_EQ(query.camera.fx_px, 70.0);
    EXPECT_DOUBLE_EQ(query.camera.fy_px, 80.0);
    EXPECT_DOUBLE_EQ(query.camera.cx_px, -5.0);
    EXPECT_DOUBLE_EQ(query.camera.cy_px, 220.0);
    EXPECT_DOUBLE_EQ(query.options.min_range_m, 500.0);
    EXPECT_EQ(terrain.calls, 0);
}

TEST_F(DesktopIo, MissingFileAndMalformedJson) {
    expectError(loadScene(directory / "missing.json", terrain), ErrorCode::IoFailure);
    for (const auto text : {"{", "[]", "null", "{\"schema_version\":1,\"schema_version\":2}"})
        expectError(loadScene(write(text), terrain));
}

TEST_F(DesktopIo, MissingFieldsAndUnsupportedVariants) {
    for (const auto field :
         {"schema_version", "timestamp_ns", "observer", "camera", "orientation"}) {
        SCOPED_TRACE(field);
        auto document = fixture("scene.json");
        document.erase(field);
        expectError(load(document));
    }
    auto document = fixture("scene.json");
    document["schema_version"] = 2;
    expectError(load(document), ErrorCode::UnsupportedFormat);
    document = fixture("scene.json");
    document["observer"]["height"]["mode"] = "sea_level";
    expectError(load(document), ErrorCode::UnsupportedFormat);
    document = fixture("scene.json");
    document["camera"]["fisheye"] = true;
    expectError(load(document), ErrorCode::UnsupportedFormat);
    document = fixture("scene.json");
    document["camera"]["fx_px"] = 1000;
    expectError(load(document));
    document["camera"].erase("horizontal_fov_deg");
    expectError(load(document)); // Incomplete intrinsics.
}

TEST_F(DesktopIo, RejectsWrongTypesAndNumericRangesWithoutTruncation) {
    const std::vector<std::pair<std::string, Json>> cases{{"/camera/width_px", 1280.5},
                                                          {"/camera/width_px", 2147483648ULL},
                                                          {"/camera/height_px", -1},
                                                          {"/camera/horizontal_fov_deg", "90"},
                                                          {"/camera/horizontal_fov_deg", 180},
                                                          {"/timestamp_ns", 1.5},
                                                          {"/timestamp_ns", 9223372036854775808ULL},
                                                          {"/timestamp_ns", -1},
                                                          {"/schema_version", true},
                                                          {"/observer/latitude_deg", 91},
                                                          {"/observer/longitude_deg", nullptr},
                                                          {"/observer/height/metres", false},
                                                          {"/observer/simulated", "yes"},
                                                          {"/orientation/pitch_deg", "bad"},
                                                          {"/orientation", nullptr},
                                                          {"/options", nullptr},
                                                          {"/options/visibility/max_samples", 3.5},
                                                          {"/options/visibility/max_samples", 0},
                                                          {"/options/visibility/sample_step_m", 0},
                                                          {"/options/max_range_m", 5}};
    for (const auto& [path, value] : cases) {
        SCOPED_TRACE(path + "=" + value.dump());
        auto document = fixture("scene.json");
        document[Json::json_pointer{path}] = value;
        expectError(load(document));
    }
    auto document = fixture("scene.json");
    document["timestamp_ns"] = std::numeric_limits<std::int64_t>::max();
    const auto maximum = load(document);
    ASSERT_TRUE(std::holds_alternative<FrameQuery>(maximum));
    EXPECT_EQ(std::get<FrameQuery>(maximum).timestamp.count(),
              std::numeric_limits<std::int64_t>::max());
}

TEST_F(DesktopIo, RejectsNonFiniteAndOverflowAfterConversion) {
    for (const auto text : {"{\"schema_version\":1e999}", "{\"schema_version\":NaN}",
                            "{\"schema_version\":Infinity}"})
        expectError(loadScene(write(text), terrain));
    auto document = fixture("scene.json");
    terrain.value = HeightSample{std::numeric_limits<double>::max()};
    document["observer"]["height"]["metres"] = std::numeric_limits<double>::max();
    expectError(load(document));
    terrain.value = HeightSample{0};
    document = fixture("scene.json");
    document["camera"]["horizontal_fov_deg"] = std::numeric_limits<double>::denorm_min();
    expectError(load(document));
}

TEST_F(DesktopIo, AboveDemRequiresValidTerrain) {
    const auto document = fixture("scene.json");
    for (const auto error : {TerrainSampleError::NoData, TerrainSampleError::OutsideCoverage}) {
        terrain.value = error;
        expectError(load(document));
    }
    terrain.value = HeightSample{std::numeric_limits<double>::quiet_NaN()};
    expectError(load(document));
}

PeakCatalog referenceCatalog() {
    Peak peak;
    peak.id = "test:peak";
    peak.name = "Szczyt & \"Żółć\"";
    peak.geometry_position = {{49, 20}, 1000};
    peak.geometry_height_source = GeometryHeightSource::VerifiedEllipsoid;
    peak.display_elevation = CatalogElevation{970, "unspecified", "fixture"};
    return PeakCatalog{{peak}};
}

FrameResult referenceFrame() {
    FrameResult frame;
    frame.timestamp = Timestamp{42};
    frame.diagnostics = {1, 0, 1, 3};
    frame.observations.push_back({"test:peak",
                                  {0, 0, 1000},
                                  {ProjectionState::InFrame, PixelPoint{640, 360}, 1000},
                                  VisibilityResult{VisibilityState::MissingTerrain, 12.5,
                                                   std::nullopt, 3, "NoData in fixture"}});
    return frame;
}

TEST_F(DesktopIo, WriterMatchesHandwrittenReferenceAndRoundTrips) {
    const auto path = directory / "result.json";
    const auto result =
        writeFrameJson(referenceFrame(), referenceCatalog(), {"test", "fixture", "", {}}, path);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(result));
    std::ifstream input(path);
    const auto document = Json::parse(input);
    EXPECT_EQ(document, fixture("result.json"));
    EXPECT_EQ(Json::parse(document.dump()), document);
}

TEST_F(DesktopIo, WriterPreservesAllStatesAndOptionalValues) {
    auto frame = referenceFrame();
    auto catalog = referenceCatalog();
    catalog.peaks[0].display_elevation.reset();
    for (const auto state : {VisibilityState::VisibleInModel, VisibilityState::OccludedInModel,
                             VisibilityState::Uncertain, VisibilityState::MissingTerrain}) {
        frame.observations[0].visibility->state = state;
        frame.observations[0].visibility->first_blocker_distance_m = 500.0;
        const auto path = directory / "result.json";
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(writeFrameJson(frame, catalog, {}, path)));
        std::ifstream input(path);
        const auto document = Json::parse(input);
        EXPECT_TRUE(document["observations"][0]["display_elevation"].is_null());
        EXPECT_EQ(document["observations"][0]["visibility"]["first_blocker_distance_m"], 500.0);
        EXPECT_EQ(document["warnings"].empty(), state != VisibilityState::MissingTerrain);
    }
    frame.observations[0].visibility.reset();
    for (const auto state : {ProjectionState::OutsideFrame, ProjectionState::BehindNearPlane}) {
        auto& projection = frame.observations[0].projection;
        projection.state = state;
        if (state == ProjectionState::BehindNearPlane)
            projection.pixel.reset();
        else
            projection.pixel = PixelPoint{1400, 360};
        const auto path = directory / "result.json";
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(writeFrameJson(frame, catalog, {}, path)));
        std::ifstream input(path);
        const auto record = Json::parse(input)["observations"][0];
        EXPECT_TRUE(record["visibility"].is_null());
        EXPECT_EQ(record["projection"]["pixel"].is_null(),
                  state == ProjectionState::BehindNearPlane);
    }
}

TEST_F(DesktopIo, VisibilityDebugWriterRecordsDatasetAndSamples) {
    VisibilityTrace trace;
    trace.result = {VisibilityState::VisibleInModel, 12.5, std::nullopt, 1, "fixture trace"};
    trace.samples.push_back(
        {0, 25.0, {{49.1, 20.1}, 1234.0}, VisibilitySampleTerrainState::Valid, 1200.0, 34.0});
    RunMetadata metadata{"test", "fixture-v1", std::string(64, 'a'), {}, true};
    const auto path = directory / "visibility-debug.json";

    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        writeVisibilityDebugJson({{"test:peak", std::move(trace)}}, metadata, path)));
    std::ifstream input(path);
    const auto document = Json::parse(input);
    EXPECT_EQ(document["schema_version"], 1);
    EXPECT_EQ(document["metadata"]["dataset_id"], "fixture-v1");
    EXPECT_TRUE(document["metadata"]["observer_simulated"]);
    ASSERT_EQ(document["traces"].size(), 1U);
    EXPECT_EQ(document["traces"][0]["peak_id"], "test:peak");
    EXPECT_EQ(document["traces"][0]["result"]["state"], "VisibleInModel");
    EXPECT_EQ(document["traces"][0]["samples"][0]["terrain_state"], "Valid");
    EXPECT_DOUBLE_EQ(document["traces"][0]["samples"][0]["clearance_m"].get<double>(), 34.0);
}

TEST_F(DesktopIo, WriterReportsIoAndRejectsNonFiniteBeforeTruncation) {
    const auto io_error = writeFrameJson(referenceFrame(), referenceCatalog(), {},
                                         directory / "missing" / "result.json");
    ASSERT_TRUE(std::holds_alternative<Error>(io_error));
    EXPECT_EQ(std::get<Error>(io_error).code, ErrorCode::IoFailure);
    const auto path = write("keep existing data");
    auto frame = referenceFrame();
    frame.observations[0].direction.azimuth_deg = std::numeric_limits<double>::infinity();
    const auto invalid = writeFrameJson(frame, referenceCatalog(), {}, path);
    ASSERT_TRUE(std::holds_alternative<Error>(invalid));
    EXPECT_EQ(std::get<Error>(invalid).code, ErrorCode::InvalidInput);
    std::ifstream input(path);
    std::string text;
    std::getline(input, text);
    EXPECT_EQ(text, "keep existing data");
}

RenderOptions referenceRenderOptions() {
    return {{200, 100, 100, 100, 100, 50}, false, std::nullopt};
}

TEST_F(DesktopIo, SvgEscapesXmlKeepsPolishTextAndUsesCameraCoordinates) {
    auto catalog = referenceCatalog();
    catalog.peaks[0].name = "Szczyt & <Turnia> Żółć \"cytat\" 'x'";
    auto frame = referenceFrame();
    frame.observations[0].projection.pixel = PixelPoint{198, 2};
    auto options = referenceRenderOptions();
    options.background_image = directory / "photo & <test>.jpg";
    const auto path = directory / "overlay.svg";

    ASSERT_TRUE(
        std::holds_alternative<std::monostate>(writeOverlaySvg(frame, catalog, options, path)));
    const auto svg = readText(path);
    EXPECT_NE(svg.find("viewBox=\"0 0 200 100\""), std::string::npos);
    EXPECT_NE(svg.find("Szczyt &amp; &lt;Turnia&gt; Żółć &quot;cytat&quot; &apos;x&apos; — 970 m"),
              std::string::npos);
    EXPECT_NE(svg.find("photo &amp; &lt;test&gt;.jpg"), std::string::npos);
    EXPECT_NE(svg.find("x=\"0\" y=\"0\" width=\"200\" height=\"100\" preserveAspectRatio=\"none\""),
              std::string::npos);
    EXPECT_NE(svg.find("data-peak-id=\"test:peak\" cx=\"198\" cy=\"2\""), std::string::npos);
}

TEST_F(DesktopIo, SvgMovesCollidingLabelsButNeverTheirEdgeMarkers) {
    auto catalog = referenceCatalog();
    catalog.peaks[0].id = "edge:a";
    catalog.peaks[0].name = "Pierwszy";
    auto second_peak = catalog.peaks[0];
    second_peak.id = "edge:b";
    second_peak.name = "Drugi";
    catalog.peaks.push_back(second_peak);
    auto frame = referenceFrame();
    frame.observations[0].id = "edge:a";
    frame.observations[0].projection.pixel = PixelPoint{198, 2};
    auto second_observation = frame.observations[0];
    second_observation.id = "edge:b";
    frame.observations.push_back(second_observation);
    const auto path = directory / "collisions.svg";

    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        writeOverlaySvg(frame, catalog, referenceRenderOptions(), path)));
    const auto svg = readText(path);
    const auto first_label =
        lineContaining(svg, "class=\"peak-label status-missing-terrain\" data-peak-id=\"edge:a\"");
    const auto second_label =
        lineContaining(svg, "class=\"peak-label status-missing-terrain\" data-peak-id=\"edge:b\"");
    ASSERT_FALSE(first_label.empty());
    ASSERT_FALSE(second_label.empty());
    EXPECT_NE(first_label, second_label);
    EXPECT_NE(svg.find("data-peak-id=\"edge:a\" cx=\"198\" cy=\"2\""), std::string::npos);
    EXPECT_NE(svg.find("data-peak-id=\"edge:b\" cx=\"198\" cy=\"2\""), std::string::npos);
}

TEST_F(DesktopIo, SvgDistinguishesMissingTerrainAndDebugOnlyOcclusion) {
    auto catalog = referenceCatalog();
    auto occluded_peak = catalog.peaks[0];
    occluded_peak.id = "test:occluded";
    occluded_peak.name = "Przesłonięty";
    catalog.peaks.push_back(occluded_peak);
    auto frame = referenceFrame();
    auto occluded = frame.observations[0];
    occluded.id = "test:occluded";
    occluded.projection.pixel = PixelPoint{30, 70};
    occluded.visibility->state = VisibilityState::OccludedInModel;
    frame.observations.push_back(occluded);

    const auto normal_path = directory / "normal.svg";
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        writeOverlaySvg(frame, catalog, referenceRenderOptions(), normal_path)));
    const auto normal = readText(normal_path);
    EXPECT_NE(
        lineContaining(normal, "data-peak-id=\"test:peak\" cx=").find("status-missing-terrain"),
        std::string::npos);
    EXPECT_EQ(normal.find("data-peak-id=\"test:occluded\""), std::string::npos);

    auto debug_options = referenceRenderOptions();
    debug_options.debug_visibility = true;
    const auto debug_path = directory / "debug.svg";
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        writeOverlaySvg(frame, catalog, debug_options, debug_path)));
    const auto debug = readText(debug_path);
    EXPECT_NE(
        lineContaining(debug, "data-peak-id=\"test:peak\" cx=").find("status-missing-terrain"),
        std::string::npos);
    EXPECT_NE(lineContaining(debug, "data-peak-id=\"test:occluded\" cx=").find("status-occluded"),
              std::string::npos);
    EXPECT_NE(debug.find("data-debug=\"true\""), std::string::npos);
}

TEST_F(DesktopIo, SvgRejectsInvalidInputBeforeTruncatingAndReportsIo) {
    const auto path = write("keep existing data");
    auto invalid_options = referenceRenderOptions();
    invalid_options.camera.width_px = 0;
    const auto invalid =
        writeOverlaySvg(referenceFrame(), referenceCatalog(), invalid_options, path);
    ASSERT_TRUE(std::holds_alternative<Error>(invalid));
    EXPECT_EQ(std::get<Error>(invalid).code, ErrorCode::InvalidInput);
    EXPECT_EQ(readText(path), "keep existing data");

    const auto io_error =
        writeOverlaySvg(referenceFrame(), referenceCatalog(), referenceRenderOptions(),
                        directory / "missing" / "x.svg");
    ASSERT_TRUE(std::holds_alternative<Error>(io_error));
    EXPECT_EQ(std::get<Error>(io_error).code, ErrorCode::IoFailure);
}
} // namespace
