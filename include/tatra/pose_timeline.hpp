#pragma once
#include <tatra/engine.hpp>
#include <vector>

namespace tatra {
struct PoseSample {
    Timestamp timestamp{0};
    CameraPose pose;
};

class PoseTimeline {
  public:
    // Samples must be non-empty, have non-negative strictly increasing
    // timestamps, and contain valid camera poses. Valid poses are normalized.
    [[nodiscard]] static Result<PoseTimeline> create(std::vector<PoseSample> samples);

    // Exact timestamps do not require an interpolation gap. Other timestamps
    // are never extrapolated and may only bridge a complete neighbouring
    // sample interval no longer than max_gap.
    [[nodiscard]] Result<CameraPose> sampleAt(Timestamp time, Timestamp max_gap) const;

  private:
    explicit PoseTimeline(std::vector<PoseSample> samples);
    std::vector<PoseSample> samples_;
};

struct ReplayFrame {
    Timestamp timestamp{0};
    std::optional<std::string> image_path;
};

struct Session {
    Observer observer;
    CameraIntrinsics camera;
    QueryOptions options;
    Timestamp max_gap{0};
    std::vector<PoseSample> poses;
    std::vector<ReplayFrame> frames;
};
} // namespace tatra
