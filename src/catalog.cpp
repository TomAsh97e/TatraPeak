#include <tatra/catalog.hpp>
#include <tatra/validation.hpp>

#include <cmath>
#include <unordered_set>

namespace tatra {
Status validateCatalog(const PeakCatalog& catalog) {
    std::unordered_set<PeakId> ids;
    ids.reserve(catalog.peaks.size());
    for (const auto& peak : catalog.peaks) {
        if (peak.id.empty())
            return Error{ErrorCode::InvalidInput, "peak id must not be empty"};
        if (!ids.insert(peak.id).second)
            return Error{ErrorCode::InvalidInput, "duplicate peak id: " + peak.id};
        const auto position = makeGeoPosition(peak.geometry_position.coord,
                                              peak.geometry_position.ellipsoid_height_m);
        if (const auto* error = std::get_if<Error>(&position))
            return Error{error->code, "peak '" + peak.id + "': " + error->message};
        if (peak.display_elevation && !std::isfinite(peak.display_elevation->metres))
            return Error{ErrorCode::InvalidInput,
                         "peak '" + peak.id + "': display elevation must be finite"};
    }
    return std::monostate{};
}
} // namespace tatra
