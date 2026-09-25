#include <tatra/engine.hpp>
#include <tatra/validation.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace tatra {
PeakEngine::PeakEngine(const ITerrain& terrain, const PeakCatalog& catalog)
    : terrain_{terrain}, catalog_{catalog} {}
Result<FrameResult> PeakEngine::evaluate(const FrameQuery& query) const {
    const auto observer =
        makeGeoPosition(query.observer.position.coord, query.observer.position.ellipsoid_height_m);
    if (const auto* error = std::get_if<Error>(&observer)) {
        return Error{error->code, "observer: " + error->message};
    }
    const auto camera = validateCamera(query.camera);
    if (const auto* error = std::get_if<Error>(&camera)) {
        return *error;
    }
    const auto pose = validatePose(query.pose);
    if (const auto* error = std::get_if<Error>(&pose)) {
        return *error;
    }
    const auto options = validateOptions(query.options);
    if (const auto* error = std::get_if<Error>(&options)) {
        return *error;
    }
    for (const Peak& peak : catalog_.peaks) {
        const auto position = makeGeoPosition(peak.geometry_position.coord,
                                              peak.geometry_position.ellipsoid_height_m);
        if (const auto* error = std::get_if<Error>(&position)) {
            return Error{error->code, "peak '" + peak.id + "': " + error->message};
        }
    }

    const LocalFrame frame{std::get<GeoPosition>(observer)};
    const auto& valid_options = std::get<QueryOptions>(options);
    FrameResult result;
    result.timestamp = query.timestamp;
    result.diagnostics.catalog_count = catalog_.peaks.size();
    result.observations.reserve(catalog_.peaks.size());
    for (const Peak& peak : catalog_.peaks) {
        const EnuPoint point = frame.toEnu(peak.geometry_position);
        const double range_m = std::hypot(point.value_m.x(), point.value_m.y(), point.value_m.z());
        if (!point.value_m.allFinite() || !std::isfinite(range_m)) {
            return Error{ErrorCode::InvalidInput,
                         "peak '" + peak.id + "': non-finite ENU distance"};
        }
        if (range_m < valid_options.min_range_m || range_m > valid_options.max_range_m) {
            ++result.diagnostics.outside_range_count;
            continue;
        }
        const auto direction = bearingElevation(point);
        if (const auto* error = std::get_if<Error>(&direction)) {
            return Error{error->code, "peak '" + peak.id + "': " + error->message};
        }
        const Projection projection =
            project(point, std::get<CameraPose>(pose), std::get<CameraIntrinsics>(camera),
                    valid_options.near_z_m);
        if (!std::isfinite(projection.camera_depth_m) ||
            (projection.pixel &&
             (!std::isfinite(projection.pixel->u) || !std::isfinite(projection.pixel->v)))) {
            return Error{ErrorCode::InvalidInput, "peak '" + peak.id + "': non-finite projection"};
        }
        ++result.diagnostics.projected_count;
        PeakObservation observation{peak.id, std::get<BearingElevation>(direction), projection,
                                    std::nullopt};
        if (projection.state == ProjectionState::InFrame) {
            observation.visibility =
                traceVisibility(query.observer, peak, terrain_, valid_options.visibility);
            result.diagnostics.visibility_sample_count += observation.visibility->sample_count;
        }
        result.observations.push_back(std::move(observation));
    }
    std::stable_sort(result.observations.begin(), result.observations.end(),
                     [](const PeakObservation& left, const PeakObservation& right) {
                         return left.id < right.id;
                     });
    return result;
}
} // namespace tatra
