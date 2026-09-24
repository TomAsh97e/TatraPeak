#pragma once
#include <tatra/types.hpp>
#include <vector>

namespace tatra {
struct PeakCatalog { std::vector<Peak> peaks; };
// TODO(T12/T13): stable IDs, finite coordinates, duplicate detection.
[[nodiscard]] Status validateCatalog(const PeakCatalog& catalog);
} // namespace tatra
