#include <tatra/desktop/io.hpp>
#include "not_implemented.hpp"
namespace tatra::desktop {
Result<Dataset> loadDataset(const std::filesystem::path&) {
    return detail::notImplemented("T13", "loadDataset");
}
} // namespace tatra::desktop
