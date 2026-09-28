#include <tatra/terrain.hpp>

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <utility>

namespace {
using namespace tatra;

TEST(GridTerrain, ClosedBoundsRejectAdjacentOutsideValuesAfterCopyAndMove) {
    // A plane h = 100 + 20*row + 4*column on exactly representable coordinates.
    auto created = GridTerrain::create({3, 4, 2.0, 10.0, -.5, .25},
                                       {100, 104, 108, 112, 120, 124, 128, 132, 140, 144, 148, 152},
                                       std::vector<std::uint8_t>(12, 255));
    ASSERT_TRUE(std::holds_alternative<GridTerrain>(created));
    auto original = std::get<GridTerrain>(std::move(created));
    auto copied = original;
    auto moved = std::move(copied);
    for (const auto* terrain : {&original, &moved}) {
        for (int row = 0; row <= 8; ++row) {
            for (int col = 0; col <= 12; ++col) {
                const auto sample = terrain->sampleHeight({2.0 - row * .125, 10.0 + col * .0625});
                ASSERT_TRUE(std::holds_alternative<HeightSample>(sample));
                EXPECT_DOUBLE_EQ(std::get<HeightSample>(sample).ellipsoid_height_m,
                                 100 + row * 5 + col);
            }
        }
        const double inf = std::numeric_limits<double>::infinity();
        for (const auto coord :
             {GeoCoord{std::nextafter(1.0, -inf), 10.0}, GeoCoord{std::nextafter(2.0, inf), 10.0},
              GeoCoord{1.0, std::nextafter(10.0, -inf)}, GeoCoord{1.0, std::nextafter(10.75, inf)},
              GeoCoord{inf, 10.0}, GeoCoord{1.0, std::numeric_limits<double>::quiet_NaN()}}) {
            const auto result = terrain->sampleHeight(coord);
            ASSERT_TRUE(std::holds_alternative<TerrainSampleError>(result));
            EXPECT_EQ(std::get<TerrainSampleError>(result), TerrainSampleError::OutsideCoverage);
        }
    }
}

TEST(GridTerrain, EdgeSamplesIgnoreZeroWeightNoDataButInteriorRequiresAllContributors) {
    auto created =
        GridTerrain::create({2, 2, 2.0, 10.0, -1.0, 1.0}, {0, -20, 10, 40}, {255, 0, 255, 255});
    ASSERT_TRUE(std::holds_alternative<GridTerrain>(created));
    const auto& terrain = std::get<GridTerrain>(created);
    EXPECT_DOUBLE_EQ(std::get<HeightSample>(terrain.sampleHeight({2.0, 10.0})).ellipsoid_height_m,
                     0);
    EXPECT_DOUBLE_EQ(std::get<HeightSample>(terrain.sampleHeight({1.0, 11.0})).ellipsoid_height_m,
                     40);
    EXPECT_DOUBLE_EQ(std::get<HeightSample>(terrain.sampleHeight({1.0, 10.5})).ellipsoid_height_m,
                     25);
    EXPECT_EQ(std::get<TerrainSampleError>(terrain.sampleHeight({1.5, 10.5})),
              TerrainSampleError::NoData);
}
} // namespace
