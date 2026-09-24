#include <tatra/pose_timeline.hpp>
#include "not_implemented.hpp"
#include <utility>

namespace tatra {
PoseTimeline::PoseTimeline(std::vector<PoseSample> samples) : samples_{std::move(samples)} {}
Result<PoseTimeline> PoseTimeline::create(std::vector<PoseSample>) {
    return detail::notImplemented("T15", "PoseTimeline::create");
}
Result<CameraPose> PoseTimeline::sampleAt(Timestamp, Timestamp) const {
    return detail::notImplemented("T15", "PoseTimeline::sampleAt");
}
} // namespace tatra
