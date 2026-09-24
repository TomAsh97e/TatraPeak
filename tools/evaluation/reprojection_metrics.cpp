#include <tatra/evaluation/reprojection_metrics.hpp>
#include <stdexcept>

namespace tatra::evaluation {
ReprojectionMetrics measureReprojection(std::span<const PeakAnnotation>,
    std::span<const PeakObservation>, const CameraIntrinsics&) {
    throw std::logic_error{"measureReprojection is not implemented; see task T17."};
}
} // namespace tatra::evaluation
