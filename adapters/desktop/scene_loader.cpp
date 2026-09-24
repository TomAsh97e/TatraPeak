#include <tatra/desktop/io.hpp>
#include "not_implemented.hpp"
namespace tatra::desktop {
Result<FrameQuery> loadScene(const std::filesystem::path&, const ITerrain&) {
    return detail::notImplemented("T08", "loadScene");
}
} // namespace tatra::desktop
