#pragma once
#include <tatra/types.hpp>
#include <vector>

namespace tatra {
// Owns peak records. Keep the catalog alive and unchanged while an engine uses it.
struct PeakCatalog {
    std::vector<Peak> peaks;
};
// TODO(T12/T13): stable IDs, finite coordinates, duplicate detection.
[[nodiscard]] Status validateCatalog(const PeakCatalog& catalog);
} // namespace tatra
