#include <tatra/angles.hpp>
#include <gtest/gtest.h>
#include <numbers>

TEST(Angles, ConvertsKnownDegreeValues) {
    EXPECT_DOUBLE_EQ(tatra::degreesToRadians(0.0), 0.0);
    EXPECT_DOUBLE_EQ(tatra::degreesToRadians(180.0), std::numbers::pi_v<double>);
    EXPECT_DOUBLE_EQ(tatra::degreesToRadians(-90.0), -std::numbers::pi_v<double> / 2.0);
}
TEST(Angles, ConvertsKnownRadianValues) {
    EXPECT_DOUBLE_EQ(tatra::radiansToDegrees(0.0), 0.0);
    EXPECT_DOUBLE_EQ(tatra::radiansToDegrees(std::numbers::pi_v<double>), 180.0);
    EXPECT_DOUBLE_EQ(tatra::radiansToDegrees(-std::numbers::pi_v<double> / 2.0), -90.0);
}
