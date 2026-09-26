#include <tatra/evaluation/reprojection_metrics.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tatra::evaluation {
double pixelError(const PixelPoint& first, const PixelPoint& second) {
    return std::hypot(first.u - second.u, first.v - second.v);
}

Eigen::Vector3d pixelToUnitRay(const PixelPoint& pixel, const CameraIntrinsics& camera) {
    return Eigen::Vector3d{(pixel.u - camera.cx_px) / camera.fx_px,
                           (pixel.v - camera.cy_px) / camera.fy_px, 1.0}
        .normalized();
}

double angularError(const Eigen::Vector3d& first_unit_ray, const Eigen::Vector3d& second_unit_ray) {
    const double denominator = first_unit_ray.norm() * second_unit_ray.norm();
    const double cosine = std::clamp(first_unit_ray.dot(second_unit_ray) / denominator, -1.0, 1.0);
    return std::acos(cosine) * 180.0 / std::numbers::pi;
}

std::optional<double> nearestRankPercentile(std::span<const double> values, double percentile) {
    if (values.empty() || !std::isfinite(percentile) || percentile < 0.0 || percentile > 100.0 ||
        std::any_of(values.begin(), values.end(),
                    [](double value) { return !std::isfinite(value); }))
        return std::nullopt;

    std::vector<double> sorted{values.begin(), values.end()};
    std::sort(sorted.begin(), sorted.end());
    if (percentile == 0.0)
        return sorted.front();
    const double rank = std::ceil(percentile * static_cast<double>(sorted.size()) / 100.0);
    return sorted[static_cast<std::size_t>(rank) - 1];
}

ReprojectionMetrics measureReprojection(std::span<const PeakAnnotation> annotations,
                                        std::span<const PeakObservation> observations,
                                        const CameraIntrinsics& camera) {
    ReprojectionMetrics metrics;
    metrics.annotated_count = annotations.size();
    metrics.prediction_count = observations.size();
    metrics.predictions_in_frame_count = static_cast<std::size_t>(std::count_if(
        observations.begin(), observations.end(), [](const PeakObservation& observation) {
            return observation.projection.state == ProjectionState::InFrame;
        }));
    metrics.points.reserve(annotations.size());

    std::unordered_map<PeakId, std::vector<const PeakObservation*>> predictions_by_id;
    predictions_by_id.reserve(observations.size());
    for (const auto& observation : observations)
        predictions_by_id[observation.id].push_back(&observation);

    std::unordered_map<PeakId, std::size_t> annotation_counts;
    annotation_counts.reserve(annotations.size());
    for (const auto& annotation : annotations)
        ++annotation_counts[annotation.id];

    std::vector<double> pixel_errors;
    std::vector<double> angular_errors;
    pixel_errors.reserve(annotations.size());
    angular_errors.reserve(annotations.size());

    for (const auto& annotation : annotations) {
        ReprojectionPointResult point;
        point.id = annotation.id;
        point.annotated_pixel = annotation.pixel;
        const auto found = predictions_by_id.find(annotation.id);
        if (annotation_counts.at(annotation.id) != 1 ||
            (found != predictions_by_id.end() && found->second.size() > 1)) {
            point.state = AnnotationEvaluationState::AmbiguousId;
            ++metrics.ambiguous_count;
            ++metrics.missing_count;
            metrics.points.push_back(std::move(point));
            continue;
        }
        if (found == predictions_by_id.end()) {
            point.state = AnnotationEvaluationState::MissingPrediction;
            ++metrics.missing_count;
            metrics.points.push_back(std::move(point));
            continue;
        }

        const auto& observation = *found->second.front();
        ++metrics.matched_count;
        point.projection_state = observation.projection.state;
        if (!observation.projection.pixel) {
            point.state = AnnotationEvaluationState::MissingPixel;
            ++metrics.missing_count;
            metrics.points.push_back(std::move(point));
            continue;
        }

        point.state = AnnotationEvaluationState::Evaluated;
        point.predicted_pixel = observation.projection.pixel;
        point.error_px = pixelError(annotation.pixel, *observation.projection.pixel);
        const auto annotated_ray = pixelToUnitRay(annotation.pixel, camera);
        const auto predicted_ray = pixelToUnitRay(*observation.projection.pixel, camera);
        point.error_deg = angularError(annotated_ray, predicted_ray);
        pixel_errors.push_back(*point.error_px);
        angular_errors.push_back(*point.error_deg);
        ++metrics.evaluated_count;
        metrics.points.push_back(std::move(point));
    }

    metrics.median_error_px = nearestRankPercentile(pixel_errors, 50.0);
    metrics.p95_error_px = nearestRankPercentile(pixel_errors, 95.0);
    metrics.median_error_deg = nearestRankPercentile(angular_errors, 50.0);
    metrics.p95_error_deg = nearestRankPercentile(angular_errors, 95.0);
    return metrics;
}
} // namespace tatra::evaluation
