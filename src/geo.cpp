#include <tatra/geo.hpp>
#include "not_implemented.hpp"

namespace tatra {
EcefPoint toEcef(const GeoPosition&) {
    detail::throwNotImplemented("T03", "toEcef");
}
GeoPosition fromEcef(const EcefPoint&) {
    detail::throwNotImplemented("T03", "fromEcef");
}
LocalFrame::LocalFrame(const GeoPosition& origin) : origin_{origin} {}
EnuPoint LocalFrame::toEnu(const GeoPosition&) const {
    detail::throwNotImplemented("T03", "LocalFrame::toEnu");
}
GeoPosition LocalFrame::fromEnu(const EnuPoint&) const {
    detail::throwNotImplemented("T03", "LocalFrame::fromEnu");
}
Result<BearingElevation> bearingElevation(const EnuPoint&) {
    return detail::notImplemented("T03", "bearingElevation");
}
} // namespace tatra
