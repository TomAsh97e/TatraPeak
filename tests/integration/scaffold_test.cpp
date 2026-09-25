#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <tatra/engine.hpp>
#include <utility>

namespace {
using namespace tatra;

class SceneTerrain final : public ITerrain {
  public:
    mutable std::size_t calls = 0;
    HeightResult sampleHeight(GeoCoord position) const override {
        ++calls;
        if (position.latitude_deg > 0.004 && position.latitude_deg < 0.006) {
            if (position.longitude_deg > 0.0005)
                return HeightSample{2000.0};
            if (position.longitude_deg < -0.0005)
                return TerrainSampleError::NoData;
        }
        return HeightSample{0.0};
    }
};

FrameQuery validQuery() {
    FrameQuery query;
    query.timestamp = Timestamp{123456};
    query.observer.position = {{0.0, 0.0}, 1000.0};
    query.camera = {1280, 720, 1000.0, 1000.0, 640.0, 360.0};
    query.pose = std::get<CameraPose>(poseFromHeadingPitchRoll(0.0, 0.0, 0.0));
    query.options.max_range_m = 5000.0;
    return query;
}

Peak peakAt(std::string id, double latitude, double longitude) {
    Peak peak;
    peak.id = std::move(id);
    peak.geometry_position = {{latitude, longitude}, 1000.0};
    peak.geometry_height_source = GeometryHeightSource::VerifiedEllipsoid;
    return peak;
}

TEST(Engine, FivePeakSceneChecksStatusesAndTerrainCallBudget) {
    SceneTerrain terrain;
    const PeakCatalog catalog{{peakAt("visible", 0.01, 0.0), peakAt("outside", 0.01, 0.02),
                               peakAt("behind", -0.01, 0.0), peakAt("occluded", 0.01, 0.003),
                               peakAt("missing", 0.01, -0.003)}};
    const auto query = validQuery();
    const auto evaluated = PeakEngine{terrain, catalog}.evaluate(query);
    ASSERT_TRUE(std::holds_alternative<FrameResult>(evaluated));
    const auto& frame = std::get<FrameResult>(evaluated);
    const auto& records = frame.observations;
    ASSERT_EQ(records.size(), 5U);
    EXPECT_EQ(frame.timestamp, query.timestamp);
    EXPECT_EQ(records[0].id, "behind");
    EXPECT_EQ(records[1].id, "missing");
    EXPECT_EQ(records[2].id, "occluded");
    EXPECT_EQ(records[3].id, "outside");
    EXPECT_EQ(records[4].id, "visible");
    EXPECT_EQ(records[0].projection.state, ProjectionState::BehindNearPlane);
    EXPECT_FALSE(records[0].projection.pixel);
    EXPECT_FALSE(records[0].visibility);
    EXPECT_EQ(records[3].projection.state, ProjectionState::OutsideFrame);
    EXPECT_TRUE(records[3].projection.pixel);
    EXPECT_FALSE(records[3].visibility);
    std::size_t samples = 0;
    for (const auto index : {1U, 2U, 4U}) {
        EXPECT_EQ(records[index].projection.state, ProjectionState::InFrame);
        ASSERT_TRUE(records[index].visibility);
        samples += records[index].visibility->sample_count;
    }
    EXPECT_EQ(records[1].visibility->state, VisibilityState::MissingTerrain);
    EXPECT_EQ(records[2].visibility->state, VisibilityState::OccludedInModel);
    EXPECT_EQ(records[4].visibility->state, VisibilityState::VisibleInModel);
    ASSERT_TRUE(records[4].projection.pixel);
    EXPECT_NEAR(records[4].projection.pixel->u, 640.0, 1e-8);
    EXPECT_NEAR(records[4].direction.azimuth_deg, 0.0, 1e-8);
    EXPECT_EQ(frame.diagnostics.catalog_count, 5U);
    EXPECT_EQ(frame.diagnostics.outside_range_count, 0U);
    EXPECT_EQ(frame.diagnostics.projected_count, 5U);
    EXPECT_EQ(frame.diagnostics.visibility_sample_count, samples);
    EXPECT_GT(samples, 0U);
    // Exactly two endpoint calls per in-frame peak, plus interior samples.
    EXPECT_EQ(terrain.calls, samples + 6U);

    SceneTerrain skipped_terrain;
    const PeakCatalog skipped{{catalog.peaks[1], catalog.peaks[2]}};
    const auto skipped_result = PeakEngine{skipped_terrain, skipped}.evaluate(query);
    ASSERT_TRUE(std::holds_alternative<FrameResult>(skipped_result));
    EXPECT_EQ(skipped_terrain.calls, 0U);
    EXPECT_EQ(std::get<FrameResult>(skipped_result).diagnostics.visibility_sample_count, 0U);
}

TEST(Engine, FiltersRangeBeforeDirectionProjectionAndTerrainAccess) {
    SceneTerrain terrain;
    const PeakCatalog catalog{
        {peakAt("near", 0.0001, 0.0), peakAt("far", 0.1, 0.0), peakAt("coincident", 0.0, 0.0)}};
    const auto evaluated = PeakEngine{terrain, catalog}.evaluate(validQuery());
    ASSERT_TRUE(std::holds_alternative<FrameResult>(evaluated));
    const auto& frame = std::get<FrameResult>(evaluated);
    EXPECT_TRUE(frame.observations.empty());
    EXPECT_EQ(frame.diagnostics.catalog_count, 3U);
    EXPECT_EQ(frame.diagnostics.outside_range_count, 3U);
    EXPECT_EQ(frame.diagnostics.projected_count, 0U);
    EXPECT_EQ(frame.diagnostics.visibility_sample_count, 0U);
    EXPECT_EQ(terrain.calls, 0U);
}

TEST(Engine, RangeBoundsAreInclusive) {
    SceneTerrain terrain;
    const PeakCatalog catalog{{peakAt("behind", -0.01, 0.0)}};
    auto query = validQuery();
    const auto point =
        LocalFrame{query.observer.position}.toEnu(catalog.peaks[0].geometry_position);
    const double range = std::hypot(point.value_m.x(), point.value_m.y(), point.value_m.z());
    for (const bool lower_bound : {true, false}) {
        query.options.min_range_m = lower_bound ? range : 0.0;
        query.options.max_range_m = lower_bound ? range + 100.0 : range;
        const auto result = PeakEngine{terrain, catalog}.evaluate(query);
        ASSERT_TRUE(std::holds_alternative<FrameResult>(result));
        EXPECT_EQ(std::get<FrameResult>(result).observations.size(), 1U);
    }
    EXPECT_EQ(terrain.calls, 0U);
}

TEST(Engine, EqualIdsRetainCatalogOrderAndResultsOwnTheirData) {
    SceneTerrain terrain;
    PeakCatalog catalog{
        {peakAt("same", -0.02, 0.0), peakAt("a", -0.01, 0.0), peakAt("same", -0.01, 0.0)}};
    const auto evaluated = PeakEngine{terrain, catalog}.evaluate(validQuery());
    ASSERT_TRUE(std::holds_alternative<FrameResult>(evaluated));
    catalog.peaks.clear();
    const auto& records = std::get<FrameResult>(evaluated).observations;
    ASSERT_EQ(records.size(), 3U);
    EXPECT_EQ(records[0].id, "a");
    EXPECT_EQ(records[1].id, "same");
    EXPECT_EQ(records[2].id, "same");
    EXPECT_GT(records[1].direction.slant_range_m, records[2].direction.slant_range_m);
}

TEST(Engine, ValidatesEveryQueryComponentEvenWithEmptyCatalog) {
    SceneTerrain terrain;
    const PeakCatalog catalog;
    const PeakEngine engine{terrain, catalog};
    for (int invalid_field = 0; invalid_field < 4; ++invalid_field) {
        auto query = validQuery();
        switch (invalid_field) {
        case 0:
            query.observer.position.coord.latitude_deg = 91.0;
            break;
        case 1:
            query.camera.fx_px = std::numeric_limits<double>::quiet_NaN();
            break;
        case 2:
            query.pose.camera_from_enu.coeffs().setZero();
            break;
        case 3:
            query.options.visibility.sample_step_m = 0.0;
            break;
        }
        const auto result = engine.evaluate(query);
        ASSERT_TRUE(std::holds_alternative<Error>(result));
        EXPECT_EQ(std::get<Error>(result).code, ErrorCode::InvalidInput);
        EXPECT_FALSE(std::get<Error>(result).message.empty());
    }
    const auto result = engine.evaluate(validQuery());
    ASSERT_TRUE(std::holds_alternative<FrameResult>(result));
    EXPECT_TRUE(std::get<FrameResult>(result).observations.empty());
    EXPECT_EQ(std::get<FrameResult>(result).diagnostics.catalog_count, 0U);
    EXPECT_EQ(terrain.calls, 0U);
}

TEST(Engine, RejectsInvalidPeakBeforeAnyTerrainAccess) {
    SceneTerrain terrain;
    PeakCatalog catalog{{peakAt("valid", 0.01, 0.0), peakAt("bad", 0.02, 0.0)}};
    catalog.peaks[1].geometry_position.ellipsoid_height_m = std::numeric_limits<double>::infinity();
    const auto result = PeakEngine{terrain, catalog}.evaluate(validQuery());
    ASSERT_TRUE(std::holds_alternative<Error>(result));
    EXPECT_EQ(std::get<Error>(result).code, ErrorCode::InvalidInput);
    EXPECT_NE(std::get<Error>(result).message.find("bad"), std::string::npos);
    EXPECT_EQ(terrain.calls, 0U);
}

TEST(Engine, UsesNormalizedPoseWithoutMutatingQuery) {
    SceneTerrain terrain;
    const PeakCatalog catalog{{peakAt("visible", 0.01, 0.0)}};
    const PeakEngine engine{terrain, catalog};
    auto query = validQuery();
    const auto reference = engine.evaluate(query);
    ASSERT_TRUE(std::holds_alternative<FrameResult>(reference));
    query.pose.camera_from_enu.coeffs() *= 1.0005;
    const auto result = engine.evaluate(query);
    ASSERT_TRUE(std::holds_alternative<FrameResult>(result));
    const auto& actual = std::get<FrameResult>(result).observations.at(0).projection;
    const auto& expected = std::get<FrameResult>(reference).observations.at(0).projection;
    ASSERT_TRUE(actual.pixel);
    ASSERT_TRUE(expected.pixel);
    EXPECT_NEAR(actual.pixel->u, expected.pixel->u, 1e-8);
    EXPECT_NEAR(actual.pixel->v, expected.pixel->v, 1e-8);
    EXPECT_NEAR(query.pose.camera_from_enu.norm(), 1.0005, 1e-12);
}

TEST(Engine, CoincidentPeakInRangeReportsUndefinedDirection) {
    SceneTerrain terrain;
    const PeakCatalog catalog{{peakAt("coincident", 0.0, 0.0)}};
    auto query = validQuery();
    query.options.min_range_m = 0.0;
    const auto result = PeakEngine{terrain, catalog}.evaluate(query);
    ASSERT_TRUE(std::holds_alternative<Error>(result));
    EXPECT_EQ(std::get<Error>(result).code, ErrorCode::InvalidInput);
    EXPECT_EQ(terrain.calls, 0U);
}
} // namespace
