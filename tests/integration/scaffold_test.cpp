#include <tatra/engine.hpp>
#include <gtest/gtest.h>

namespace {
class MissingTerrain final : public tatra::ITerrain {
public:
    tatra::HeightResult sampleHeight(tatra::GeoCoord) const override {
        return tatra::TerrainSampleError::NoData;
    }
};
} // namespace

// Replace with the five-object scene from T07 when implementing the engine.
TEST(Scaffold, UnfinishedEngineDoesNotReportSuccessfulAnalysis) {
    const MissingTerrain terrain;
    const tatra::PeakCatalog catalog;
    const tatra::PeakEngine engine{terrain, catalog};
    const auto result = engine.evaluate(tatra::FrameQuery{});
    ASSERT_TRUE(std::holds_alternative<tatra::Error>(result));
    EXPECT_EQ(std::get<tatra::Error>(result).code, tatra::ErrorCode::NotImplemented);
}
