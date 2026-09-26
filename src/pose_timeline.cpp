#include <tatra/pose_timeline.hpp>
#include <tatra/validation.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

namespace tatra {
namespace {
Error invalid(std::string message) { return {ErrorCode::InvalidInput, std::move(message)}; }
} // namespace

PoseTimeline::PoseTimeline(std::vector<PoseSample> samples) : samples_{std::move(samples)} {}

Result<PoseTimeline> PoseTimeline::create(std::vector<PoseSample> samples) {
    if (samples.empty())
        return invalid("Pose timeline must contain at least one sample");

    for (std::size_t index = 0; index < samples.size(); ++index) {
        auto& sample = samples[index];
        if (sample.timestamp.count() < 0)
            return invalid("Pose sample timestamp must be non-negative at index " +
                           std::to_string(index));
        if (index > 0 && sample.timestamp <= samples[index - 1].timestamp)
            return invalid("Pose sample timestamps must be strictly increasing at index " +
                           std::to_string(index));
        auto valid_pose = validatePose(sample.pose);
        if (const auto* error = std::get_if<Error>(&valid_pose))
            return Error{error->code, "Invalid pose sample at index " + std::to_string(index) +
                                          ": " + error->message};
        sample.pose = std::get<CameraPose>(std::move(valid_pose));
    }
    return PoseTimeline{std::move(samples)};
}

Result<CameraPose> PoseTimeline::sampleAt(Timestamp time, Timestamp max_gap) const {
    if (max_gap.count() < 0)
        return invalid("max_gap must be non-negative");

    const auto right = std::lower_bound(
        samples_.begin(), samples_.end(), time,
        [](const PoseSample& sample, Timestamp requested) { return sample.timestamp < requested; });
    if (right != samples_.end() && right->timestamp == time)
        return right->pose;
    if (right == samples_.begin() || right == samples_.end())
        return Error{ErrorCode::TimeOutsideSession,
                     "Requested pose time is outside the timeline range"};

    const auto left = std::prev(right);
    const Timestamp gap = right->timestamp - left->timestamp;
    if (gap > max_gap)
        return Error{ErrorCode::TimeGapTooLarge, "Neighbouring pose samples exceed max_gap"};

    const double fraction =
        static_cast<double>((time - left->timestamp).count()) / static_cast<double>(gap.count());
    Eigen::Quaterniond left_rotation = left->pose.camera_from_enu;
    Eigen::Quaterniond right_rotation = right->pose.camera_from_enu;
    if (left_rotation.dot(right_rotation) < 0.0)
        right_rotation.coeffs() *= -1.0;
    CameraPose interpolated{left_rotation.slerp(fraction, right_rotation)};
    interpolated.camera_from_enu.normalize();
    return interpolated;
}
} // namespace tatra
