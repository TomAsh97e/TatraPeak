#include <tatra/mobile.h>

#include <tatra/engine.hpp>
#include <tatra/version.hpp>

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct tatra_mobile_engine {
    tatra::GridTerrain terrain;
    tatra::PeakCatalog catalog;
    tatra::PeakEngine engine;
    std::unordered_map<tatra::PeakId, const tatra::Peak*> peaks_by_id;

    tatra_mobile_engine(tatra::GridTerrain value, tatra::PeakCatalog peaks)
        : terrain{std::move(value)}, catalog{std::move(peaks)}, engine{terrain, catalog} {
        peaks_by_id.reserve(catalog.peaks.size());
        for (const auto& peak : catalog.peaks)
            peaks_by_id.emplace(peak.id, &peak);
    }
};

namespace {

void writeError(char* output, std::size_t capacity, const std::string& message) {
    if (output == nullptr || capacity == 0)
        return;
    const auto count = std::min(capacity - 1, message.size());
    std::memcpy(output, message.data(), count);
    output[count] = '\0';
}

tatra_mobile_status invalid(char* output, std::size_t capacity, const std::string& message) {
    writeError(output, capacity, message);
    return TATRA_MOBILE_INVALID_ARGUMENT;
}

tatra_mobile_status fromCoreError(const tatra::Error& error, char* output, std::size_t capacity) {
    writeError(output, capacity, error.message);
    return error.code == tatra::ErrorCode::InvalidInput ? TATRA_MOBILE_INVALID_ARGUMENT
                                                        : TATRA_MOBILE_CORE_ERROR;
}

tatra::FrameQuery toCoreQuery(const tatra_mobile_query& value) {
    tatra::FrameQuery query;
    query.timestamp = tatra::Timestamp{value.timestamp_ns};
    query.observer.position = {{value.observer_latitude_deg, value.observer_longitude_deg},
                               value.observer_ellipsoid_height_m};
    query.observer.simulated = value.observer_simulated != 0;
    query.pose.camera_from_enu =
        Eigen::Quaterniond{value.camera_from_enu_w, value.camera_from_enu_x,
                           value.camera_from_enu_y, value.camera_from_enu_z};
    query.camera = {value.image_width_px, value.image_height_px, value.fx_px,
                    value.fy_px,          value.cx_px,           value.cy_px};
    query.options.min_range_m = value.min_range_m;
    query.options.max_range_m = value.max_range_m;
    query.options.near_z_m = value.near_z_m;
    query.options.visibility.sample_step_m = value.visibility_sample_step_m;
    query.options.visibility.clearance_band_m = value.visibility_clearance_band_m;
    query.options.visibility.start_guard_m = value.visibility_start_guard_m;
    query.options.visibility.target_guard_m = value.visibility_target_guard_m;
    query.options.visibility.max_samples = value.visibility_max_samples;
    return query;
}

tatra_mobile_projection_state projectionState(tatra::ProjectionState state) {
    switch (state) {
    case tatra::ProjectionState::InFrame:
        return TATRA_MOBILE_IN_FRAME;
    case tatra::ProjectionState::OutsideFrame:
        return TATRA_MOBILE_OUTSIDE_FRAME;
    case tatra::ProjectionState::BehindNearPlane:
        return TATRA_MOBILE_BEHIND_NEAR_PLANE;
    }
    return TATRA_MOBILE_BEHIND_NEAR_PLANE;
}

tatra_mobile_visibility_state visibilityState(tatra::VisibilityState state) {
    switch (state) {
    case tatra::VisibilityState::VisibleInModel:
        return TATRA_MOBILE_VISIBLE_IN_MODEL;
    case tatra::VisibilityState::OccludedInModel:
        return TATRA_MOBILE_OCCLUDED_IN_MODEL;
    case tatra::VisibilityState::Uncertain:
        return TATRA_MOBILE_UNCERTAIN;
    case tatra::VisibilityState::MissingTerrain:
        return TATRA_MOBILE_MISSING_TERRAIN;
    }
    return TATRA_MOBILE_UNCERTAIN;
}

} // namespace

extern "C" {

tatra_mobile_status tatra_mobile_engine_create(const tatra_mobile_create_info* info,
                                               tatra_mobile_engine** out_engine,
                                               char* error_message, size_t error_message_capacity) {
    if (error_message != nullptr && error_message_capacity > 0)
        error_message[0] = '\0';
    if (info == nullptr || out_engine == nullptr)
        return invalid(error_message, error_message_capacity, "info and out_engine are required");
    *out_engine = nullptr;
    const auto& grid = info->terrain;
    if ((error_message == nullptr) != (error_message_capacity == 0))
        return TATRA_MOBILE_INVALID_ARGUMENT;
    if (grid.rows != 0 && grid.columns > std::numeric_limits<std::size_t>::max() / grid.rows)
        return invalid(error_message, error_message_capacity, "terrain dimensions overflow size_t");
    const auto expected_samples = grid.rows * grid.columns;
    if (grid.rows < 2 || grid.columns < 2)
        return invalid(error_message, error_message_capacity,
                       "terrain rows and columns must be at least 2");
    if (grid.sample_count != expected_samples)
        return invalid(error_message, error_message_capacity,
                       "terrain sample_count must equal rows*columns");
    if (expected_samples > 0 && (grid.heights_ellipsoid_m == nullptr || grid.valid_mask == nullptr))
        return invalid(error_message, error_message_capacity,
                       "terrain height and mask arrays are required");
    if (info->peak_count > 0 && info->peaks == nullptr)
        return invalid(error_message, error_message_capacity, "peaks are required");

    try {
        std::vector<float> heights(grid.heights_ellipsoid_m,
                                   grid.heights_ellipsoid_m + expected_samples);
        std::vector<std::uint8_t> mask(grid.valid_mask, grid.valid_mask + expected_samples);
        auto terrain = tatra::GridTerrain::create(
            {grid.rows, grid.columns, grid.first_center_latitude_deg,
             grid.first_center_longitude_deg, grid.latitude_step_deg, grid.longitude_step_deg},
            std::move(heights), std::move(mask));
        if (const auto* error = std::get_if<tatra::Error>(&terrain))
            return fromCoreError(*error, error_message, error_message_capacity);

        tatra::PeakCatalog catalog;
        catalog.peaks.reserve(info->peak_count);
        for (std::size_t index = 0; index < info->peak_count; ++index) {
            const auto& source = info->peaks[index];
            if (source.id == nullptr)
                return invalid(error_message, error_message_capacity,
                               "every peak id must be a non-null string");
            tatra::Peak peak;
            peak.id = source.id;
            peak.name = source.name != nullptr ? source.name : "";
            peak.geometry_position = {{source.latitude_deg, source.longitude_deg},
                                      source.ellipsoid_height_m};
            peak.geometry_height_source = tatra::GeometryHeightSource::ExplicitEllipsoid;
            if (source.has_display_elevation != 0)
                peak.display_elevation = {source.display_elevation_m, "unspecified",
                                          "mobile input"};
            catalog.peaks.push_back(std::move(peak));
        }
        const auto catalog_status = tatra::validateCatalog(catalog);
        if (const auto* error = std::get_if<tatra::Error>(&catalog_status))
            return fromCoreError(*error, error_message, error_message_capacity);
        *out_engine = new tatra_mobile_engine{std::get<tatra::GridTerrain>(std::move(terrain)),
                                              std::move(catalog)};
        return TATRA_MOBILE_OK;
    } catch (const std::bad_alloc&) {
        writeError(error_message, error_message_capacity, "allocation failed");
        return TATRA_MOBILE_INTERNAL_ERROR;
    } catch (const std::exception& error) {
        writeError(error_message, error_message_capacity, error.what());
        return TATRA_MOBILE_INTERNAL_ERROR;
    } catch (...) {
        writeError(error_message, error_message_capacity, "unknown internal error");
        return TATRA_MOBILE_INTERNAL_ERROR;
    }
}

void tatra_mobile_engine_destroy(tatra_mobile_engine* engine) { delete engine; }

size_t tatra_mobile_engine_peak_count(const tatra_mobile_engine* engine) {
    return engine != nullptr ? engine->catalog.peaks.size() : 0;
}

tatra_mobile_status
tatra_mobile_engine_evaluate(tatra_mobile_engine* engine, const tatra_mobile_query* query,
                             tatra_mobile_observation* observations, size_t observation_capacity,
                             size_t* out_count, tatra_mobile_diagnostics* diagnostics,
                             char* error_message, size_t error_message_capacity) {
    if (error_message != nullptr && error_message_capacity > 0)
        error_message[0] = '\0';
    if ((error_message == nullptr) != (error_message_capacity == 0))
        return TATRA_MOBILE_INVALID_ARGUMENT;
    if (engine == nullptr || query == nullptr || out_count == nullptr || diagnostics == nullptr)
        return invalid(error_message, error_message_capacity,
                       "engine, query, out_count and diagnostics are required");
    if (observation_capacity > 0 && observations == nullptr)
        return invalid(error_message, error_message_capacity,
                       "observations are required when capacity is non-zero");
    *out_count = 0;
    *diagnostics = {};

    try {
        auto evaluated = engine->engine.evaluate(toCoreQuery(*query));
        if (const auto* error = std::get_if<tatra::Error>(&evaluated))
            return fromCoreError(*error, error_message, error_message_capacity);
        const auto result = std::get<tatra::FrameResult>(std::move(evaluated));
        *out_count = result.observations.size();
        diagnostics->catalog_count = result.diagnostics.catalog_count;
        diagnostics->outside_range_count = result.diagnostics.outside_range_count;
        diagnostics->projected_count = result.diagnostics.projected_count;
        diagnostics->visibility_sample_count = result.diagnostics.visibility_sample_count;
        if (observation_capacity < result.observations.size()) {
            writeError(error_message, error_message_capacity,
                       "observation buffer is too small; use peak_count as capacity");
            return TATRA_MOBILE_BUFFER_TOO_SMALL;
        }
        for (std::size_t index = 0; index < result.observations.size(); ++index) {
            const auto& source = result.observations[index];
            auto& destination = observations[index];
            destination = {};
            const auto peak = engine->peaks_by_id.find(source.id);
            if (peak == engine->peaks_by_id.end()) {
                writeError(error_message, error_message_capacity,
                           "core returned an unknown peak identifier");
                return TATRA_MOBILE_INTERNAL_ERROR;
            }
            destination.peak_id = peak->second->id.c_str();
            destination.peak_name = peak->second->name.c_str();
            if (peak->second->display_elevation) {
                destination.has_display_elevation = 1;
                destination.display_elevation_m = peak->second->display_elevation->metres;
            }
            destination.azimuth_deg = source.direction.azimuth_deg;
            destination.elevation_deg = source.direction.elevation_deg;
            destination.slant_range_m = source.direction.slant_range_m;
            destination.projection_state = projectionState(source.projection.state);
            destination.camera_depth_m = source.projection.camera_depth_m;
            if (source.projection.pixel) {
                destination.has_pixel = 1;
                destination.pixel_u = source.projection.pixel->u;
                destination.pixel_v = source.projection.pixel->v;
            }
            destination.visibility_state = TATRA_MOBILE_VISIBILITY_NOT_EVALUATED;
            if (source.visibility) {
                destination.visibility_state = visibilityState(source.visibility->state);
                destination.visibility_sample_count = source.visibility->sample_count;
                if (source.visibility->min_sampled_clearance_m) {
                    destination.has_min_sampled_clearance = 1;
                    destination.min_sampled_clearance_m =
                        *source.visibility->min_sampled_clearance_m;
                }
                if (source.visibility->first_blocker_distance_m) {
                    destination.has_first_blocker_distance = 1;
                    destination.first_blocker_distance_m =
                        *source.visibility->first_blocker_distance_m;
                }
            }
        }
        return TATRA_MOBILE_OK;
    } catch (const std::bad_alloc&) {
        writeError(error_message, error_message_capacity, "allocation failed");
        return TATRA_MOBILE_INTERNAL_ERROR;
    } catch (const std::exception& error) {
        writeError(error_message, error_message_capacity, error.what());
        return TATRA_MOBILE_INTERNAL_ERROR;
    } catch (...) {
        writeError(error_message, error_message_capacity, "unknown internal error");
        return TATRA_MOBILE_INTERNAL_ERROR;
    }
}

uint32_t tatra_mobile_abi_version(void) { return TATRA_MOBILE_ABI_VERSION; }

const char* tatra_mobile_version(void) { return tatra::version.data(); }

} // extern "C"
