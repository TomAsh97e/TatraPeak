#pragma once
#include <tatra/types.hpp>
#include <vector>

namespace tatra {
struct PoseSample { Timestamp timestamp{0}; CameraPose pose; };

class PoseTimeline {
public:
    [[nodiscard]] static Result<PoseTimeline> create(std::vector<PoseSample> samples);
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
    std::vector<PoseSample> poses;
    std::vector<ReplayFrame> frames;
};
} // namespace tatra
