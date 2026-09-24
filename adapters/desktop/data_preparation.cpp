#include <tatra/desktop/io.hpp>
#include "not_implemented.hpp"
namespace tatra::desktop {
Status prepareDataset(const PrepareOptions&) {
    // TODO(T11-T13): normalize DEM, import OSM, validate and write the manifest.
    return detail::notImplemented("T11-T13", "prepareDataset");
}
} // namespace tatra::desktop
