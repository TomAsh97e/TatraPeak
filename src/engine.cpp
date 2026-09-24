#include <tatra/engine.hpp>
#include "not_implemented.hpp"

namespace tatra {
PeakEngine::PeakEngine(const ITerrain& terrain, const PeakCatalog& catalog)
    : terrain_{terrain}, catalog_{catalog} {}
Result<FrameResult> PeakEngine::evaluate(const FrameQuery&) const {
    return detail::notImplemented("T07", "PeakEngine::evaluate");
}
} // namespace tatra
