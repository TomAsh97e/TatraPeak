#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <tatra/desktop/io.hpp>
#include <tatra/validation.hpp>
#include <unordered_map>
#include <utility>

namespace tatra::desktop {
namespace {
using Json = nlohmann::json;
Json finite(double value) {
    if (!std::isfinite(value))
        throw Error{ErrorCode::InvalidInput, "Cannot serialize a non-finite result number"};
    return value;
}
Json optionalNumber(const std::optional<double>& value) {
    return value ? finite(*value) : Json(nullptr);
}
const char* stateName(ProjectionState state) {
    switch (state) {
    case ProjectionState::InFrame:
        return "InFrame";
    case ProjectionState::OutsideFrame:
        return "OutsideFrame";
    case ProjectionState::BehindNearPlane:
        return "BehindNearPlane";
    }
    throw Error{ErrorCode::InvalidInput, "Unknown projection state"};
}
const char* stateName(VisibilityState state) {
    switch (state) {
    case VisibilityState::VisibleInModel:
        return "VisibleInModel";
    case VisibilityState::OccludedInModel:
        return "OccludedInModel";
    case VisibilityState::Uncertain:
        return "Uncertain";
    case VisibilityState::MissingTerrain:
        return "MissingTerrain";
    }
    throw Error{ErrorCode::InvalidInput, "Unknown visibility state"};
}
} // namespace

Status writeFrameJson(const FrameResult& frame, const PeakCatalog& catalog,
                      const RunMetadata& metadata, const std::filesystem::path& output) {
    try {
        const auto valid_options = validateOptions(metadata.options);
        if (const auto* error = std::get_if<Error>(&valid_options))
            return *error;
        std::unordered_map<PeakId, const Peak*> peaks;
        for (const auto& peak : catalog.peaks) {
            if (!peaks.emplace(peak.id, &peak).second)
                return Error{ErrorCode::InvalidInput, "Ambiguous duplicate peak id: " + peak.id};
        }
        const auto& options = metadata.options;
        const auto& visibility = options.visibility;
        Json document = {{"schema_version", 1},
                         {"timestamp_ns", frame.timestamp.count()},
                         {"metadata",
                          {{"program_version", metadata.program_version},
                           {"dataset_id", metadata.dataset_id},
                           {"dataset_sha256", metadata.dataset_sha256}}},
                         {"options",
                          {{"min_range_m", options.min_range_m},
                           {"max_range_m", options.max_range_m},
                           {"near_z_m", options.near_z_m},
                           {"visibility",
                            {{"sample_step_m", visibility.sample_step_m},
                             {"clearance_band_m", visibility.clearance_band_m},
                             {"start_guard_m", visibility.start_guard_m},
                             {"target_guard_m", visibility.target_guard_m},
                             {"max_samples", visibility.max_samples}}}}},
                         {"diagnostics",
                          {{"catalog_count", frame.diagnostics.catalog_count},
                           {"outside_range_count", frame.diagnostics.outside_range_count},
                           {"projected_count", frame.diagnostics.projected_count},
                           {"visibility_sample_count", frame.diagnostics.visibility_sample_count}}},
                         {"observations", Json::array()},
                         {"warnings", Json::array()}};
        bool missing_terrain = false;
        for (const auto& observation : frame.observations) {
            const auto found = peaks.find(observation.id);
            if (found == peaks.end())
                return Error{ErrorCode::InvalidInput,
                             "Unknown peak id in result: " + observation.id};
            const auto& peak = *found->second;
            const auto& position = peak.geometry_position;
            const auto valid_position =
                makeGeoPosition(position.coord, position.ellipsoid_height_m);
            if (const auto* error = std::get_if<Error>(&valid_position))
                return *error;
            const auto& projection = observation.projection;
            Json pixel = nullptr;
            if (projection.pixel)
                pixel = {{"u", finite(projection.pixel->u)}, {"v", finite(projection.pixel->v)}};
            Json record = {{"id", peak.id},
                           {"name", peak.name},
                           {"geometry_position",
                            {{"latitude_deg", position.coord.latitude_deg},
                             {"longitude_deg", position.coord.longitude_deg},
                             {"ellipsoid_height_m", position.ellipsoid_height_m}}},
                           {"display_elevation", nullptr},
                           {"direction",
                            {{"azimuth_deg", finite(observation.direction.azimuth_deg)},
                             {"elevation_deg", finite(observation.direction.elevation_deg)},
                             {"slant_range_m", finite(observation.direction.slant_range_m)}}},
                           {"projection",
                            {{"state", stateName(projection.state)},
                             {"pixel", pixel},
                             {"camera_depth_m", finite(projection.camera_depth_m)}}},
                           {"visibility", nullptr}};
            switch (peak.geometry_height_source) {
            case GeometryHeightSource::DemSample:
                record["geometry_height_source"] = "DemSample";
                break;
            case GeometryHeightSource::VerifiedEllipsoid:
                record["geometry_height_source"] = "VerifiedEllipsoid";
                break;
            default:
                return Error{ErrorCode::InvalidInput, "Unknown geometry height source"};
            }
            if (peak.display_elevation) {
                record["display_elevation"] = {
                    {"metres", finite(peak.display_elevation->metres)},
                    {"vertical_reference", peak.display_elevation->vertical_reference},
                    {"source", peak.display_elevation->source}};
            }
            if (observation.visibility) {
                const auto& result = *observation.visibility;
                record["visibility"] = {
                    {"state", stateName(result.state)},
                    {"min_sampled_clearance_m", optionalNumber(result.min_sampled_clearance_m)},
                    {"first_blocker_distance_m", optionalNumber(result.first_blocker_distance_m)},
                    {"sample_count", result.sample_count},
                    {"reason", result.reason}};
                missing_terrain =
                    missing_terrain || result.state == VisibilityState::MissingTerrain;
            }
            document["observations"].push_back(std::move(record));
        }
        if (missing_terrain)
            document["warnings"].push_back(
                {{"code", "MissingTerrain"},
                 {"message", "Terrain data is incomplete for one or more lines of sight."}});
        // Serialize and validate UTF-8 before opening (and potentially truncating) output.
        const auto serialized = document.dump(2) + '\n';
        std::ofstream stream;
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(output, std::ios::binary | std::ios::trunc);
        stream << serialized;
        stream.close();
        return std::monostate{};
    } catch (const Error& error) {
        return error;
    } catch (const Json::exception& error) {
        return Error{ErrorCode::InvalidInput,
                     "Cannot serialize result JSON: " + std::string{error.what()}};
    } catch (const std::ios_base::failure& error) {
        return Error{ErrorCode::IoFailure, "Cannot write result: " + std::string{error.what()}};
    }
}
} // namespace tatra::desktop
