#pragma once
#include <tatra/terrain.hpp>
#include <cstddef>

namespace tatra {
enum class VisibilityState { VisibleInModel, OccludedInModel, Uncertain, MissingTerrain };

struct VisibilityOptions {
    double sample_step_m = 10.0;
    double clearance_band_m = 0.5;
    double start_guard_m = 2.0;
    double target_guard_m = 30.0;
    std::size_t max_samples = 10000;
};

struct VisibilityResult {
    VisibilityState state = VisibilityState::Uncertain;
    std::optional<double> min_sampled_clearance_m;
    std::optional<double> first_blocker_distance_m;
    std::size_t sample_count = 0;
    std::string reason;
};

// TODO(T06): sampled ECEF line of sight; uncertainty is part of the result.
[[nodiscard]] VisibilityResult traceVisibility(const Observer& observer,
    const Peak& peak, const ITerrain& terrain, const VisibilityOptions& options);
} // namespace tatra
