#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <tatra/desktop/io.hpp>

namespace {
using namespace std::chrono_literals;
using namespace tatra;
using namespace tatra::desktop;
using Json = nlohmann::json;

class FlatTerrain final : public ITerrain {
  public:
    HeightResult sampleHeight(GeoCoord) const override { return HeightSample{0.0}; }
};

class SessionReplay : public testing::Test {
  protected:
    std::filesystem::path directory;
    FlatTerrain terrain;

    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() / ("tatra-t16-" + std::to_string(stamp));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
    }

    void TearDown() override { std::filesystem::remove_all(directory); }

    std::filesystem::path generate(const char* name, Timestamp duration = 1s, Timestamp delay = 0ns,
                                   double heading_error = 0.0) {
        const auto path = directory / name;
        const auto generated = generateSyntheticSession({path, heading_error, delay, duration});
        EXPECT_TRUE(std::holds_alternative<std::monostate>(generated));
        return path;
    }

    Session load(const std::filesystem::path& path) {
        auto loaded = loadSession(path, terrain);
        EXPECT_TRUE(std::holds_alternative<Session>(loaded));
        return std::get<Session>(std::move(loaded));
    }

    PeakCatalog catalog() const {
        Peak peak;
        peak.id = "test:north";
        peak.name = "North";
        peak.geometry_position = {{0.01, 0.0}, 1000.0};
        peak.geometry_height_source = GeometryHeightSource::VerifiedEllipsoid;
        return {{peak}};
    }

    ReplayResult replay(const Session& session) {
        auto result = replaySession(session, terrain, catalog());
        EXPECT_TRUE(std::holds_alternative<ReplayResult>(result));
        return std::get<ReplayResult>(std::move(result));
    }

    static double middleU(const ReplayResult& replay_result) {
        const auto& result = *replay_result.frames.at(1).result;
        EXPECT_EQ(result.observations.size(), 1U);
        EXPECT_TRUE(result.observations.front().projection.pixel.has_value());
        return result.observations.front().projection.pixel->u;
    }

    static std::string read(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }

    static void write(const std::filesystem::path& path, const Json& document) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << document.dump(2) << '\n';
    }
};

TEST_F(SessionReplay, LoadsNamedQuaternionSessionAndOptionalFrameImage) {
    const auto path = generate("load");
    auto frames = Json::parse(read(path / "frames.json"));
    frames[1]["image_path"] = "images/middle.jpg";
    write(path / "frames.json", frames);

    const auto session = load(path);
    ASSERT_EQ(session.poses.size(), 3U);
    EXPECT_EQ(session.poses.front().timestamp, 0ns);
    EXPECT_NEAR(session.poses.front().pose.camera_from_enu.norm(), 1.0, 1e-15);
    EXPECT_EQ(session.max_gap, 1s);
    EXPECT_TRUE(session.observer.simulated);
    ASSERT_TRUE(session.frames[1].image_path.has_value());
    EXPECT_EQ(*session.frames[1].image_path, "images/middle.jpg");
}

TEST_F(SessionReplay, RejectsPositionalQuaternionDuplicateTimeAndEscapingPath) {
    const auto positional = generate("positional");
    auto poses = Json::parse(read(positional / "poses.json"));
    poses[0]["camera_from_enu"] = Json::array({1.0, 0.0, 0.0, 0.0});
    write(positional / "poses.json", poses);
    auto result = loadSession(positional, terrain);
    ASSERT_TRUE(std::holds_alternative<Error>(result));
    EXPECT_EQ(std::get<Error>(result).code, ErrorCode::InvalidInput);

    const auto duplicate = generate("duplicate");
    poses = Json::parse(read(duplicate / "poses.json"));
    poses[1]["timestamp_ns"] = 0;
    write(duplicate / "poses.json", poses);
    result = loadSession(duplicate, terrain);
    ASSERT_TRUE(std::holds_alternative<Error>(result));
    EXPECT_EQ(std::get<Error>(result).code, ErrorCode::InvalidInput);

    const auto escaping = generate("escaping");
    auto session = Json::parse(read(escaping / "session.json"));
    session["poses_file"] = "../poses.json";
    write(escaping / "session.json", session);
    result = loadSession(escaping, terrain);
    ASSERT_TRUE(std::holds_alternative<Error>(result));
    EXPECT_EQ(std::get<Error>(result).code, ErrorCode::InvalidInput);
}

TEST_F(SessionReplay, SameSessionProducesByteIdenticalSemanticFrameResults) {
    const auto session = load(generate("repeat"));
    const auto first = replay(session);
    const auto second = replay(session);
    ASSERT_EQ(first.completed_count, 3U);
    ASSERT_EQ(second.completed_count, 3U);
    const auto peaks = catalog();
    const RunMetadata metadata{"test", "synthetic", "hash", session.options, true};
    for (std::size_t index = 0; index < first.frames.size(); ++index) {
        const auto first_path = directory / ("first-" + std::to_string(index) + ".json");
        const auto second_path = directory / ("second-" + std::to_string(index) + ".json");
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            writeFrameJson(*first.frames[index].result, peaks, metadata, first_path)));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            writeFrameJson(*second.frames[index].result, peaks, metadata, second_path)));
        EXPECT_EQ(read(first_path), read(second_path));
    }
}

TEST_F(SessionReplay, HeadingErrorAndPoseDelayMoveProjectionInExpectedDirections) {
    const auto baseline = replay(load(generate("baseline")));
    const auto heading_error = replay(load(generate("heading-error", 1s, 0ns, 10.0)));
    const auto delayed = replay(load(generate("delayed", 1s, 250ms)));

    EXPECT_LT(middleU(heading_error), middleU(baseline));
    EXPECT_GT(middleU(delayed), middleU(baseline));
}

TEST_F(SessionReplay, MissingPoseIsRecordedAndDoesNotReuseLastOrientation) {
    auto session = load(generate("missing"));
    session.frames.push_back({1500ms, std::nullopt});
    const auto result = replay(session);

    ASSERT_EQ(result.completed_count, 3U);
    ASSERT_EQ(result.skipped_count, 1U);
    ASSERT_TRUE(result.frames.back().error.has_value());
    EXPECT_EQ(result.frames.back().error->code, ErrorCode::TimeOutsideSession);
    EXPECT_FALSE(result.frames.back().result.has_value());
}

TEST_F(SessionReplay, ScalingAllDataTimestampsDoesNotChangeSemanticResults) {
    const auto normal = replay(load(generate("normal-speed", 1s)));
    const auto slower = replay(load(generate("half-speed", 2s)));
    ASSERT_EQ(normal.completed_count, slower.completed_count);
    ASSERT_EQ(normal.frames.size(), slower.frames.size());
    for (std::size_t index = 0; index < normal.frames.size(); ++index) {
        const auto& first = normal.frames[index].result->observations.front();
        const auto& second = slower.frames[index].result->observations.front();
        EXPECT_EQ(first.projection.state, second.projection.state);
        ASSERT_EQ(first.projection.pixel.has_value(), second.projection.pixel.has_value());
        if (first.projection.pixel) {
            EXPECT_NEAR(first.projection.pixel->u, second.projection.pixel->u, 1e-12);
            EXPECT_NEAR(first.projection.pixel->v, second.projection.pixel->v, 1e-12);
        }
    }
}

TEST_F(SessionReplay, ReportContainsSkippedErrorCountsAndSessionMetadata) {
    auto session = load(generate("report"));
    session.frames.push_back({2s, std::nullopt});
    auto result = replay(session);
    const auto output = directory / "report.json";
    const RunMetadata metadata{"test", "synthetic", "abc", session.options, true};
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        writeReplayReportJson(result, session, metadata, output)));
    const auto document = Json::parse(read(output));
    EXPECT_EQ(document["metadata"]["dataset_id"], "synthetic");
    EXPECT_EQ(document["session"]["max_gap_ns"], 1000000000);
    EXPECT_EQ(document["summary"]["completed_count"], 3);
    EXPECT_EQ(document["summary"]["skipped_count"], 1);
    EXPECT_EQ(document["frames"][3]["status"], "skipped");
    EXPECT_EQ(document["frames"][3]["error"]["code"], "TimeOutsideSession");
}
} // namespace
