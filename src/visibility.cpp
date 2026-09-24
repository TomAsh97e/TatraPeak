#include <tatra/visibility.hpp>
#include "not_implemented.hpp"

namespace tatra {
VisibilityResult traceVisibility(const Observer&, const Peak&, const ITerrain&,
    const VisibilityOptions&) {
    detail::throwNotImplemented("T06", "traceVisibility");
}
} // namespace tatra
