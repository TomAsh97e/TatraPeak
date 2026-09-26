#include <tatra/geo.hpp>
#include <tatra/visibility.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace tatra {

namespace {

VisibilityResult uncertainResult(std::string reason) {
    return VisibilityResult{VisibilityState::Uncertain, std::nullopt, std::nullopt, 0,
                            std::move(reason)};
}

std::optional<std::string> validateVisibilityOptions(const VisibilityOptions& options) {
    if (!std::isfinite(options.sample_step_m) || options.sample_step_m <= 0.0) {
        return "sample_step_m must be finite and greater than zero";
    }
    if (!std::isfinite(options.clearance_band_m) || options.clearance_band_m < 0.0) {
        return "clearance_band_m must be finite and non-negative";
    }
    if (!std::isfinite(options.start_guard_m) || options.start_guard_m < 0.0) {
        return "start_guard_m must be finite and non-negative";
    }
    if (!std::isfinite(options.target_guard_m) || options.target_guard_m < 0.0) {
        return "target_guard_m must be finite and non-negative";
    }
    if (options.max_samples == 0) {
        return "max_samples must be greater than zero";
    }
    return std::nullopt;
}

VisibilityResult traceVisibilityImpl(const Observer& observer, const Peak& peak,
                                     const ITerrain& terrain, const VisibilityOptions& options,
                                     std::vector<VisibilityDebugSample>* debug_samples) {
    if (const auto error = validateVisibilityOptions(options)) {
        return uncertainResult("invalid visibility options: " + *error);
    }

    const EcefPoint observer_ecef = toEcef(observer.position);
    const EcefPoint peak_ecef = toEcef(peak.geometry_position);
    if (!observer_ecef.value_m.allFinite() || !peak_ecef.value_m.allFinite()) {
        return uncertainResult("observer and peak positions must produce finite ECEF coordinates");
    }

    const Eigen::Vector3d ray_vector_m = peak_ecef.value_m - observer_ecef.value_m;
    const double ray_length_m = ray_vector_m.norm();
    if (!std::isfinite(ray_length_m)) {
        return uncertainResult("observer-to-peak distance must be finite");
    }

    bool missing_terrain = false;
    std::string missing_reason;
    const auto inspectEndpoint = [&](const GeoPosition& position,
                                     const char* endpoint_name) -> std::optional<VisibilityResult> {
        const HeightResult height = terrain.sampleHeight(position.coord);
        if (const auto* sample = std::get_if<HeightSample>(&height)) {
            if (!std::isfinite(sample->ellipsoid_height_m)) {
                if (!missing_terrain) {
                    missing_reason = std::string{"terrain returned a non-finite height at the "} +
                                     endpoint_name + " endpoint";
                }
                missing_terrain = true;
                return std::nullopt;
            }
            const double clearance_m = position.ellipsoid_height_m - sample->ellipsoid_height_m;
            if (!std::isfinite(clearance_m)) {
                return uncertainResult(std::string{endpoint_name} +
                                       " terrain clearance is non-finite");
            }
            if (clearance_m < -options.clearance_band_m) {
                return uncertainResult(std::string{endpoint_name} +
                                       " is below the modeled terrain beyond the clearance band");
            }
            return std::nullopt;
        }

        if (!missing_terrain) {
            const auto sample_error = std::get<TerrainSampleError>(height);
            missing_reason =
                sample_error == TerrainSampleError::OutsideCoverage
                    ? std::string{endpoint_name} + " endpoint is outside terrain coverage"
                    : std::string{"terrain is NoData at the "} + endpoint_name + " endpoint";
        }
        missing_terrain = true;
        return std::nullopt;
    };

    if (const auto result = inspectEndpoint(observer.position, "observer")) {
        return *result;
    }
    if (const auto result = inspectEndpoint(peak.geometry_position, "peak")) {
        return *result;
    }

    const double guard_length_m = options.start_guard_m + options.target_guard_m;
    if (!std::isfinite(guard_length_m)) {
        return uncertainResult("combined endpoint guards must be finite");
    }
    const double checked_length_m = ray_length_m - guard_length_m;
    if (checked_length_m <= 0.0) {
        return uncertainResult("line of sight is too short after applying endpoint guards");
    }
    if (guard_length_m > 0.0 && guard_length_m >= checked_length_m) {
        return uncertainResult("endpoint guards dominate the line of sight");
    }

    const double required_samples =
        std::max(1.0, std::ceil(checked_length_m / options.sample_step_m));
    const double size_limit = static_cast<double>(std::numeric_limits<std::size_t>::max());
    if (!std::isfinite(required_samples) ||
        required_samples > static_cast<double>(options.max_samples) ||
        required_samples >= size_limit) {
        return uncertainResult("required line-of-sight samples exceed max_samples");
    }

    const std::size_t planned_samples = static_cast<std::size_t>(required_samples);
    if (planned_samples == 0) {
        return uncertainResult("line of sight produced no interior samples");
    }

    VisibilityResult result;
    result.state = VisibilityState::VisibleInModel;
    const double segment_length_m = checked_length_m / static_cast<double>(planned_samples);
    bool sample_in_clearance_band = false;

    for (std::size_t sample_index = 0; sample_index < planned_samples; ++sample_index) {
        const double distance_from_observer_m =
            options.start_guard_m + (static_cast<double>(sample_index) + 0.5) * segment_length_m;
        const double ray_fraction = distance_from_observer_m / ray_length_m;
        const EcefPoint ray_ecef{observer_ecef.value_m + ray_fraction * ray_vector_m};
        const GeoPosition ray_position = fromEcef(ray_ecef);
        ++result.sample_count;

        if (!std::isfinite(ray_position.ellipsoid_height_m) ||
            !std::isfinite(ray_position.coord.latitude_deg) ||
            !std::isfinite(ray_position.coord.longitude_deg)) {
            return VisibilityResult{
                VisibilityState::Uncertain, result.min_sampled_clearance_m, std::nullopt,
                result.sample_count,
                "line-of-sight sample produced non-finite geodetic coordinates"};
        }

        const HeightResult terrain_height = terrain.sampleHeight(ray_position.coord);
        const auto* height_sample = std::get_if<HeightSample>(&terrain_height);
        if (height_sample == nullptr || !std::isfinite(height_sample->ellipsoid_height_m)) {
            if (debug_samples) {
                VisibilitySampleTerrainState state = VisibilitySampleTerrainState::NonFinite;
                if (height_sample == nullptr) {
                    state = std::get<TerrainSampleError>(terrain_height) ==
                                    TerrainSampleError::OutsideCoverage
                                ? VisibilitySampleTerrainState::OutsideCoverage
                                : VisibilitySampleTerrainState::NoData;
                }
                debug_samples->push_back({sample_index, distance_from_observer_m, ray_position,
                                          state, std::nullopt, std::nullopt});
            }
            if (!missing_terrain) {
                if (height_sample != nullptr) {
                    missing_reason = "terrain returned a non-finite height along the line of sight";
                } else {
                    const auto sample_error = std::get<TerrainSampleError>(terrain_height);
                    missing_reason = sample_error == TerrainSampleError::OutsideCoverage
                                         ? "line of sight leaves terrain coverage"
                                         : "terrain is NoData along the line of sight";
                }
            }
            missing_terrain = true;
            continue;
        }

        const double clearance_m =
            ray_position.ellipsoid_height_m - height_sample->ellipsoid_height_m;
        if (debug_samples) {
            debug_samples->push_back(
                {sample_index, distance_from_observer_m, ray_position,
                 VisibilitySampleTerrainState::Valid, height_sample->ellipsoid_height_m,
                 std::isfinite(clearance_m) ? std::optional<double>{clearance_m} : std::nullopt});
        }
        if (!std::isfinite(clearance_m)) {
            return VisibilityResult{VisibilityState::Uncertain, result.min_sampled_clearance_m,
                                    std::nullopt, result.sample_count,
                                    "sampled terrain clearance is non-finite"};
        }
        if (!result.min_sampled_clearance_m || clearance_m < *result.min_sampled_clearance_m) {
            result.min_sampled_clearance_m = clearance_m;
        }

        if (clearance_m < -options.clearance_band_m) {
            result.state = VisibilityState::OccludedInModel;
            result.first_blocker_distance_m = distance_from_observer_m;
            result.reason = "terrain exceeds the ray height beyond the clearance band";
            return result;
        }
        if (std::abs(clearance_m) <= options.clearance_band_m) {
            sample_in_clearance_band = true;
        }
    }

    if (missing_terrain) {
        result.state = VisibilityState::MissingTerrain;
        result.reason = std::move(missing_reason);
        return result;
    }
    if (sample_in_clearance_band) {
        result.state = VisibilityState::Uncertain;
        result.reason = "one or more samples fall within the clearance tolerance band";
        return result;
    }

    result.reason = "all sampled terrain clearances exceed the clearance band";
    return result;
}

} // namespace

VisibilityResult traceVisibility(const Observer& observer, const Peak& peak,
                                 const ITerrain& terrain, const VisibilityOptions& options) {
    return traceVisibilityImpl(observer, peak, terrain, options, nullptr);
}

VisibilityTrace traceVisibilityDebug(const Observer& observer, const Peak& peak,
                                     const ITerrain& terrain, const VisibilityOptions& options) {
    VisibilityTrace trace;
    trace.result = traceVisibilityImpl(observer, peak, terrain, options, &trace.samples);
    return trace;
}
} // namespace tatra
