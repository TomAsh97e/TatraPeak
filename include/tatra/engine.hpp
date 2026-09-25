#pragma once
#include <tatra/camera.hpp>
#include <tatra/catalog.hpp>
#include <tatra/geo.hpp>
#include <tatra/visibility.hpp>

namespace tatra {
struct QueryOptions {
    // Inclusive slant-range bounds in metres (not surface or trail distance).
    double min_range_m = 500.0;
    double max_range_m = 30000.0;
    double near_z_m = 0.01;
    VisibilityOptions visibility;
};

struct FrameQuery {
    Timestamp timestamp{0};
    Observer observer;
    CameraPose pose;
    CameraIntrinsics camera;
    QueryOptions options;
};

struct PeakObservation {
    PeakId id;
    BearingElevation direction;
    Projection projection;
    std::optional<VisibilityResult> visibility; // Present only for InFrame.
};

struct FrameDiagnostics {
    std::size_t catalog_count = 0;
    std::size_t outside_range_count = 0;
    std::size_t projected_count = 0;         // All projections, including outside/behind camera.
    std::size_t visibility_sample_count = 0; // Interior samples; excludes endpoint checks.
};

struct FrameResult {
    Timestamp timestamp{0};
    std::vector<PeakObservation> observations;
    FrameDiagnostics diagnostics;
};

class PeakEngine {
  public:
    // The terrain and catalog must outlive the engine and remain unchanged.
    PeakEngine(const ITerrain& terrain, const PeakCatalog& catalog);
    PeakEngine(ITerrain&&, const PeakCatalog&) = delete;
    PeakEngine(const ITerrain&, PeakCatalog&&) = delete;
    // Validates query and peak geometry before terrain access. Returned records own
    // their data and are stably sorted by id. Equal ids retain catalog order.
    [[nodiscard]] Result<FrameResult> evaluate(const FrameQuery& query) const;

  private:
    const ITerrain& terrain_;
    const PeakCatalog& catalog_;
};
} // namespace tatra
