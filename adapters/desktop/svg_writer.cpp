#include <tatra/desktop/io.hpp>
#include "not_implemented.hpp"
namespace tatra::desktop {
Status writeOverlaySvg(const FrameResult&, const PeakCatalog&, const RenderOptions&,
    const std::filesystem::path&) {
    return detail::notImplemented("T09", "writeOverlaySvg");
}
} // namespace tatra::desktop
