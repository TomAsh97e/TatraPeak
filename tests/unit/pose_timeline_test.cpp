#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <tatra/camera.hpp>
#include <tatra/pose_timeline.hpp>
#include <utility>
#include <vector>

namespace {
using namespace std::chrono_literals;
using namespace tatra;

CameraPose heading(double degrees) {
    const auto result = poseFromHeadingPitchRoll(degrees, 0.0, 0.0);
    EXPECT_TRUE(std::holds_alternative<CameraPose>(result));
    return std::get<CameraPose>(result);
}

PoseSample sample(Timestamp timestamp, CameraPose pose) { return {timestamp, std::move(pose)}; }

double rotationDistance(const CameraPose& first, const CameraPose& second) {
    return first.camera_from_enu.angularDistance(second.camera_from_enu);
}

void expectError(const Result<PoseTimeline>& result, ErrorCode code = ErrorCode::InvalidInput) {
    ASSERT_TRUE(std::holds_alternative<Error>(result));
    EXPECT_EQ(std::get<Error>(result).code, code);
    EXPECT_FALSE(std::get<Error>(result).message.empty());
}

void expectError(const Result<CameraPose>& result, ErrorCode code) {
    ASSERT_TRUE(std::holds_alternative<Error>(result));
    EXPECT_EQ(std::get<Error>(result).code, code);
    EXPECT_FALSE(std::get<Error>(result).message.empty());
}

PoseTimeline timeline(std::vector<PoseSample> samples) {
    auto result = PoseTimeline::create(std::move(samples));
    EXPECT_TRUE(std::holds_alternative<PoseTimeline>(result));
    return std::get<PoseTimeline>(std::move(result));
}

TEST(PoseTimeline, InterpolatesHalfOfKnownNinetyDegreeRotation) {
    const auto poses = timeline({sample(0ns, heading(0.0)), sample(1s, heading(90.0))});
    const auto result = poses.sampleAt(500ms, 1s);
    ASSERT_TRUE(std::holds_alternative<CameraPose>(result));
    EXPECT_LT(rotationDistance(std::get<CameraPose>(result), heading(45.0)), 1e-12);
}

TEST(PoseTimeline, HeadingWrapUsesShortestPathFrom359To1Degrees) {
    const auto poses = timeline({sample(0ns, heading(359.0)), sample(1s, heading(1.0))});
    const auto result = poses.sampleAt(500ms, 1s);
    ASSERT_TRUE(std::holds_alternative<CameraPose>(result));
    EXPECT_LT(rotationDistance(std::get<CameraPose>(result), heading(0.0)), 1e-12);
}

TEST(PoseTimeline, OppositeQuaternionSignsDescribeTheSameRotation) {
    const auto pose = heading(37.0);
    auto opposite = pose;
    opposite.camera_from_enu.coeffs() *= -1.0;
    const auto poses = timeline({sample(0ns, pose), sample(1s, opposite)});
    const auto result = poses.sampleAt(500ms, 1s);
    ASSERT_TRUE(std::holds_alternative<CameraPose>(result));
    EXPECT_LT(rotationDistance(std::get<CameraPose>(result), pose), 1e-12);
}

TEST(PoseTimeline, ExactTimestampReturnsNormalizedStoredSampleEvenWithZeroGap) {
    auto pose = heading(20.0);
    pose.camera_from_enu.coeffs() *= 1.0005;
    const auto poses = timeline({sample(10ns, pose), sample(20ns, heading(30.0))});
    const auto result = poses.sampleAt(10ns, 0ns);
    ASSERT_TRUE(std::holds_alternative<CameraPose>(result));
    EXPECT_NEAR(std::get<CameraPose>(result).camera_from_enu.norm(), 1.0, 1e-15);
    EXPECT_LT(rotationDistance(std::get<CameraPose>(result), heading(20.0)), 1e-12);
}

TEST(PoseTimeline, SingleSampleSupportsOnlyItsExactTimestamp) {
    const auto poses = timeline({sample(10ns, heading(15.0))});
    EXPECT_TRUE(std::holds_alternative<CameraPose>(poses.sampleAt(10ns, 0ns)));
    expectError(poses.sampleAt(9ns, 1s), ErrorCode::TimeOutsideSession);
    expectError(poses.sampleAt(11ns, 1s), ErrorCode::TimeOutsideSession);
}

TEST(PoseTimeline, RejectsEmptyNegativeDuplicateAndNonMonotonicSamples) {
    expectError(PoseTimeline::create({}));
    expectError(PoseTimeline::create({sample(-1ns, heading(0.0))}));
    expectError(PoseTimeline::create({sample(1ns, heading(0.0)), sample(1ns, heading(10.0))}));
    expectError(PoseTimeline::create({sample(2ns, heading(0.0)), sample(1ns, heading(10.0))}));
}

TEST(PoseTimeline, RejectsZeroNonFiniteAndBadlyScaledQuaternions) {
    CameraPose zero;
    zero.camera_from_enu.coeffs().setZero();
    expectError(PoseTimeline::create({sample(0ns, zero)}));

    auto non_finite = heading(0.0);
    non_finite.camera_from_enu.x() = std::numeric_limits<double>::infinity();
    expectError(PoseTimeline::create({sample(0ns, non_finite)}));

    auto scaled = heading(0.0);
    scaled.camera_from_enu.coeffs() *= 2.0;
    expectError(PoseTimeline::create({sample(0ns, scaled)}));
}

TEST(PoseTimeline, DoesNotExtrapolateOutsideTimeline) {
    const auto poses = timeline({sample(10ns, heading(0.0)), sample(20ns, heading(10.0))});
    expectError(poses.sampleAt(9ns, 10ns), ErrorCode::TimeOutsideSession);
    expectError(poses.sampleAt(21ns, 10ns), ErrorCode::TimeOutsideSession);
}

TEST(PoseTimeline, EnforcesWholeNeighbourIntervalAgainstMaximumGap) {
    const auto poses = timeline({sample(0ns, heading(0.0)), sample(100ns, heading(10.0))});
    expectError(poses.sampleAt(50ns, 0ns), ErrorCode::TimeGapTooLarge);
    expectError(poses.sampleAt(50ns, 99ns), ErrorCode::TimeGapTooLarge);
    EXPECT_TRUE(std::holds_alternative<CameraPose>(poses.sampleAt(50ns, 100ns)));
    expectError(poses.sampleAt(50ns, -1ns), ErrorCode::InvalidInput);
    EXPECT_TRUE(std::holds_alternative<CameraPose>(poses.sampleAt(0ns, 0ns)));
}

TEST(PoseTimeline, SamplingFrequencyDoesNotChangeKnownTrajectory) {
    const auto sparse = timeline({sample(0ns, heading(0.0)), sample(1s, heading(90.0))});
    const auto dense = timeline(
        {sample(0ns, heading(0.0)), sample(500ms, heading(45.0)), sample(1s, heading(90.0))});
    for (const auto time : {250ms, 500ms, 750ms}) {
        const auto sparse_pose = sparse.sampleAt(time, 1s);
        const auto dense_pose = dense.sampleAt(time, 1s);
        ASSERT_TRUE(std::holds_alternative<CameraPose>(sparse_pose));
        ASSERT_TRUE(std::holds_alternative<CameraPose>(dense_pose));
        EXPECT_LT(
            rotationDistance(std::get<CameraPose>(sparse_pose), std::get<CameraPose>(dense_pose)),
            1e-12);
    }
}
} // namespace
