#pragma once
#include <tatra/types.hpp>
#include <vector>

namespace tatra {
// Owns peak records. Keep the catalog alive and unchanged while an engine uses it.
struct PeakCatalog {
    std::vector<Peak> peaks;
};
// Validates stable non-empty unique IDs, geometry and optional display elevations.
[[nodiscard]] Status validateCatalog(const PeakCatalog& catalog);
} // namespace tatra
