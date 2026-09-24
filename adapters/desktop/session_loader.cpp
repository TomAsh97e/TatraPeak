#include <tatra/desktop/io.hpp>
#include "not_implemented.hpp"
namespace tatra::desktop {
Result<Session> loadSession(const std::filesystem::path&, const ITerrain&) {
    return detail::notImplemented("T16", "loadSession");
}
} // namespace tatra::desktop
