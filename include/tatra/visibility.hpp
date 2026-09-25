#pragma once
#include <cstddef>
#include <tatra/terrain.hpp>

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
    // Minimum over visited interior samples with valid terrain, not a strict
    // minimum over the continuous line of sight.
    std::optional<double> min_sampled_clearance_m;
    std::optional<double> first_blocker_distance_m;
    // Number of visited interior line-of-sight samples. The two endpoint
    // consistency checks are not included.
    std::size_t sample_count = 0;
    std::string reason;
};

// Samples the guarded observer-to-peak segment in ECEF. Terrain is queried at
// the geodetic position of each ray sample, so ellipsoid curvature remains in
// the model. Known occlusion takes precedence over earlier missing samples.
[[nodiscard]] VisibilityResult traceVisibility(const Observer& observer, const Peak& peak,
                                               const ITerrain& terrain,
                                               const VisibilityOptions& options);
} // namespace tatra
