#pragma once
#include <optional>
#include <span>
#include <tatra/engine.hpp>
#include <vector>

namespace tatra::evaluation {
struct PeakAnnotation {
    PeakId id;
    PixelPoint pixel;
};

enum class AnnotationEvaluationState { Evaluated, MissingPrediction, MissingPixel, AmbiguousId };

struct ReprojectionPointResult {
    PeakId id;
    PixelPoint annotated_pixel;
    std::optional<PixelPoint> predicted_pixel;
    std::optional<ProjectionState> projection_state;
    AnnotationEvaluationState state = AnnotationEvaluationState::MissingPrediction;
    std::optional<double> error_px;
    std::optional<double> error_deg;
};

struct ReprojectionMetrics {
    std::size_t annotated_count = 0;
    std::size_t prediction_count = 0;
    std::size_t predictions_in_frame_count = 0;
    std::size_t matched_count = 0;
    std::size_t evaluated_count = 0;
    std::size_t missing_count = 0;
    std::size_t ambiguous_count = 0;
    std::optional<double> median_error_px;
    std::optional<double> p95_error_px;
    std::optional<double> median_error_deg;
    std::optional<double> p95_error_deg;
    std::vector<ReprojectionPointResult> points;
};

// Inputs are expected to contain finite pixels and validated camera intrinsics.
[[nodiscard]] double pixelError(const PixelPoint& first, const PixelPoint& second);
[[nodiscard]] Eigen::Vector3d pixelToUnitRay(const PixelPoint& pixel,
                                             const CameraIntrinsics& camera);
[[nodiscard]] double angularError(const Eigen::Vector3d& first_unit_ray,
                                  const Eigen::Vector3d& second_unit_ray);
// Uses the nearest-rank definition. Percentile is in [0, 100]; 0 returns the minimum.
// Empty/non-finite input or a percentile outside this range returns nullopt.
[[nodiscard]] std::optional<double> nearestRankPercentile(std::span<const double> values,
                                                          double percentile);

// OutsideFrame predictions retain a pixel and are evaluated. Missing pixels,
// absent predictions and ambiguous ids stay in the denominator as missing.
[[nodiscard]] ReprojectionMetrics measureReprojection(std::span<const PeakAnnotation> annotations,
                                                      std::span<const PeakObservation> observations,
                                                      const CameraIntrinsics& camera);
} // namespace tatra::evaluation
