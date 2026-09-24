#pragma once
#include <tatra/engine.hpp>
#include <span>

namespace tatra::evaluation {
struct PeakAnnotation { PeakId id; PixelPoint pixel; };

struct ReprojectionMetrics {
    std::size_t annotated_count = 0;
    std::size_t matched_count = 0;
    std::size_t missing_count = 0;
    std::optional<double> median_error_px;
    std::optional<double> p95_error_px;
    std::optional<double> median_error_deg;
    std::optional<double> p95_error_deg;
};

// TODO(T17): metrics must account for missing predictions.
[[nodiscard]] ReprojectionMetrics measureReprojection(
    std::span<const PeakAnnotation> annotations,
    std::span<const PeakObservation> observations,
    const CameraIntrinsics& camera);
} // namespace tatra::evaluation
