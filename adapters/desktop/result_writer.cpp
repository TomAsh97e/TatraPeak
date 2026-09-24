#include <tatra/desktop/io.hpp>
#include "not_implemented.hpp"
namespace tatra::desktop {
Status writeFrameJson(const FrameResult&, const PeakCatalog&, const RunMetadata&,
    const std::filesystem::path&) {
    return detail::notImplemented("T08", "writeFrameJson");
}
} // namespace tatra::desktop
