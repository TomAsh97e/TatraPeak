#include <cmath>
#include <gtest/gtest.h>
#include <tatra/evaluation/reprojection_metrics.hpp>
#include <utility>
#include <vector>

namespace {
using namespace tatra;
using namespace tatra::evaluation;

constexpr CameraIntrinsics camera{1280, 720, 1000.0, 1000.0, 640.0, 360.0};

PeakObservation observation(PeakId id, ProjectionState state, std::optional<PixelPoint> pixel) {
    return {std::move(id), {}, {state, pixel, 1.0}, std::nullopt};
}

TEST(ReprojectionMetrics, PixelDifferenceThreeFourHasLengthFive) {
    EXPECT_DOUBLE_EQ(pixelError({10.0, 20.0}, {13.0, 24.0}), 5.0);
}

TEST(ReprojectionMetrics, IdenticalRaysHaveZeroAngularError) {
    const Eigen::Vector3d ray{0.25, -0.5, 1.0};
    EXPECT_DOUBLE_EQ(angularError(ray, ray), 0.0);
}

TEST(ReprojectionMetrics, KnownCameraRaysHaveFortyFiveDegreeError) {
    const auto centre = pixelToUnitRay({640.0, 360.0}, camera);
    const auto one_focal_length_right = pixelToUnitRay({1640.0, 360.0}, camera);
    EXPECT_NEAR(centre.x(), 0.0, 1e-15);
    EXPECT_NEAR(centre.y(), 0.0, 1e-15);
    EXPECT_NEAR(centre.z(), 1.0, 1e-15);
    EXPECT_NEAR(angularError(centre, one_focal_length_right), 45.0, 1e-12);
}

TEST(ReprojectionMetrics, DotProductRoundingIsClampedBeforeAcos) {
    const double slightly_above_one = std::nextafter(1.0, 2.0);
    const Eigen::Vector3d first{slightly_above_one, slightly_above_one, 0.0};
    const Eigen::Vector3d second = first;
    const double result = angularError(first, second);
    EXPECT_TRUE(std::isfinite(result));
    EXPECT_NEAR(result, 0.0, 1e-12);
}

TEST(ReprojectionMetrics, NearestRankMedianAndP95MatchKnownValues) {
    const std::vector<double> values{5.0, 1.0, 4.0, 2.0, 3.0};
    ASSERT_TRUE(nearestRankPercentile(values, 50.0).has_value());
    ASSERT_TRUE(nearestRankPercentile(values, 95.0).has_value());
    EXPECT_DOUBLE_EQ(*nearestRankPercentile(values, 50.0), 3.0);
    EXPECT_DOUBLE_EQ(*nearestRankPercentile(values, 95.0), 5.0);
    EXPECT_DOUBLE_EQ(*nearestRankPercentile(values, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(*nearestRankPercentile(values, 100.0), 5.0);
    EXPECT_FALSE(nearestRankPercentile({}, 50.0).has_value());
    EXPECT_FALSE(nearestRankPercentile(values, -1.0).has_value());
    EXPECT_FALSE(nearestRankPercentile(values, 101.0).has_value());
}

TEST(ReprojectionMetrics, NoMatchingIdsProducesNullMetricsAndAllMissing) {
    const std::vector<PeakAnnotation> annotations{{"a", {10.0, 10.0}}, {"b", {20.0, 20.0}}};
    const std::vector<PeakObservation> predictions{
        observation("other", ProjectionState::InFrame, PixelPoint{10.0, 10.0})};
    const auto metrics = measureReprojection(annotations, predictions, camera);

    EXPECT_EQ(metrics.annotated_count, 2U);
    EXPECT_EQ(metrics.prediction_count, 1U);
    EXPECT_EQ(metrics.predictions_in_frame_count, 1U);
    EXPECT_EQ(metrics.matched_count, 0U);
    EXPECT_EQ(metrics.evaluated_count, 0U);
    EXPECT_EQ(metrics.missing_count, 2U);
    EXPECT_FALSE(metrics.median_error_px.has_value());
    EXPECT_FALSE(metrics.p95_error_px.has_value());
    EXPECT_FALSE(metrics.median_error_deg.has_value());
    EXPECT_FALSE(metrics.p95_error_deg.has_value());
    ASSERT_EQ(metrics.points.size(), 2U);
    EXPECT_EQ(metrics.points[0].state, AnnotationEvaluationState::MissingPrediction);
}

TEST(ReprojectionMetrics, OutsideFrameIsEvaluatedWhileMissingPixelsRemainInDenominator) {
    const std::vector<PeakAnnotation> annotations{
        {"outside", {10.0, 10.0}}, {"behind", {20.0, 20.0}}, {"absent", {30.0, 30.0}}};
    const std::vector<PeakObservation> predictions{
        observation("outside", ProjectionState::OutsideFrame, PixelPoint{13.0, 14.0}),
        observation("behind", ProjectionState::BehindNearPlane, std::nullopt),
        observation("unannotated", ProjectionState::InFrame, PixelPoint{100.0, 100.0})};
    const auto metrics = measureReprojection(annotations, predictions, camera);

    EXPECT_EQ(metrics.annotated_count, 3U);
    EXPECT_EQ(metrics.prediction_count, 3U);
    EXPECT_EQ(metrics.predictions_in_frame_count, 1U);
    EXPECT_EQ(metrics.matched_count, 2U);
    EXPECT_EQ(metrics.evaluated_count, 1U);
    EXPECT_EQ(metrics.missing_count, 2U);
    ASSERT_TRUE(metrics.median_error_px.has_value());
    ASSERT_TRUE(metrics.p95_error_px.has_value());
    EXPECT_DOUBLE_EQ(*metrics.median_error_px, 5.0);
    EXPECT_DOUBLE_EQ(*metrics.p95_error_px, 5.0);
    ASSERT_EQ(metrics.points.size(), 3U);
    EXPECT_EQ(metrics.points[0].state, AnnotationEvaluationState::Evaluated);
    EXPECT_EQ(metrics.points[0].projection_state, ProjectionState::OutsideFrame);
    EXPECT_EQ(metrics.points[1].state, AnnotationEvaluationState::MissingPixel);
    EXPECT_EQ(metrics.points[2].state, AnnotationEvaluationState::MissingPrediction);
}

TEST(ReprojectionMetrics, DuplicateAnnotationOrPredictionIdsAreAmbiguousNotCherryPicked) {
    const std::vector<PeakAnnotation> annotations{{"annotation-duplicate", {1.0, 1.0}},
                                                  {"annotation-duplicate", {2.0, 2.0}},
                                                  {"prediction-duplicate", {3.0, 3.0}}};
    const std::vector<PeakObservation> predictions{
        observation("annotation-duplicate", ProjectionState::InFrame, PixelPoint{1.0, 1.0}),
        observation("prediction-duplicate", ProjectionState::InFrame, PixelPoint{3.0, 3.0}),
        observation("prediction-duplicate", ProjectionState::InFrame, PixelPoint{4.0, 4.0})};
    const auto metrics = measureReprojection(annotations, predictions, camera);

    EXPECT_EQ(metrics.annotated_count, 3U);
    EXPECT_EQ(metrics.evaluated_count, 0U);
    EXPECT_EQ(metrics.missing_count, 3U);
    EXPECT_EQ(metrics.ambiguous_count, 3U);
    EXPECT_FALSE(metrics.median_error_px.has_value());
    for (const auto& point : metrics.points)
        EXPECT_EQ(point.state, AnnotationEvaluationState::AmbiguousId);
}
} // namespace
