#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <tatra/engine.hpp>
#include <tatra/pose_timeline.hpp>
#include <vector>

namespace tatra::desktop {
struct DatasetMetadata {
    int schema_version = 1;
    std::string dataset_id;
    std::string manifest_sha256;
    std::string horizontal_reference;
    std::string vertical_reference;
    std::string source_vertical_reference;
    std::string geoid_model;
    std::string geoid_sha256;
    std::size_t terrain_rows = 0;
    std::size_t terrain_columns = 0;
    std::size_t peak_count = 0;
};

struct Dataset {
    DatasetMetadata metadata;
    PeakCatalog catalog;
    GridTerrain terrain;
};

struct RunMetadata {
    std::string program_version;
    std::string dataset_id;
    std::string dataset_sha256;
    QueryOptions options;
    bool observer_simulated = false;
};

struct VisibilityDebugRecord {
    PeakId peak_id;
    VisibilityTrace trace;
};

struct RenderOptions {
    CameraIntrinsics camera;
    bool debug_visibility = false;
    std::optional<std::filesystem::path> background_image;
};

struct ReplayFrameResult {
    std::size_t index = 0;
    Timestamp timestamp{0};
    std::optional<std::string> image_path;
    std::optional<FrameResult> result;
    std::optional<Error> error;
    std::optional<std::string> result_path;
    std::optional<std::string> overlay_path;
};

struct ReplayResult {
    std::vector<ReplayFrameResult> frames;
    std::size_t completed_count = 0;
    std::size_t skipped_count = 0;
};

struct SyntheticSessionOptions {
    std::filesystem::path output_directory;
    double heading_error_deg = 0.0;
    Timestamp pose_delay{0};
    Timestamp duration{std::chrono::seconds{1}};
};

[[nodiscard]] Result<Dataset> loadDataset(const std::filesystem::path& directory);
[[nodiscard]] Status validateDataset(const Dataset& dataset);

struct DatasetManifestOptions {
    std::string dataset_id;
    std::filesystem::path terrain;
    std::filesystem::path peaks;
    std::filesystem::path source_metadata;
    std::filesystem::path attribution;
    std::filesystem::path output_directory;
};

[[nodiscard]] Status writeDatasetManifest(const DatasetManifestOptions& options);

struct DemGeoTiff {
    GridGeometry geometry;
    std::array<double, 6> geo_transform{};
    std::vector<float> height_values_m;
    std::vector<std::uint8_t> valid_mask;
    std::optional<double> no_data_value;
    std::string projection_wkt;
    std::string area_or_point;
    std::string vertical_reference;
    std::string source_vertical_reference;
    std::string geoid_model;
    std::string geoid_sha256;
    std::string normalization_formula;
    std::vector<std::string> source_files;
    std::vector<std::string> source_sha256;
};

using GeoidHeightProvider = std::function<Result<double>(GeoCoord)>;

[[nodiscard]] Result<DemGeoTiff> readDemGeoTiff(const std::filesystem::path& file);
[[nodiscard]] Result<GeoCoord> pixelCenterGeoCoord(const DemGeoTiff& raster, std::size_t row,
                                                   std::size_t column);
[[nodiscard]] Result<DemGeoTiff>
normalizeHeightGridToEllipsoid(const DemGeoTiff& raster, const GeoidHeightProvider& geoid_height,
                               std::string geoid_model, std::string geoid_sha256);
[[nodiscard]] Status writeNormalizedGeoTiff(const DemGeoTiff& raster,
                                            const std::filesystem::path& output);
[[nodiscard]] Result<GridTerrain> loadNormalizedGeoTiff(const std::filesystem::path& file);

struct OsmPeakNames {
    std::optional<std::string> name;
    std::optional<std::string> name_pl;
    std::optional<std::string> name_sk;
};

struct OsmPeakNode {
    PeakId id;
    std::uint64_t osm_id = 0;
    GeoCoord coord;
    OsmPeakNames source_names;
    std::optional<std::string> source_ele;
    bool source_ele_has_invalid_type = false;
    std::string selected_name;
    std::optional<CatalogElevation> display_elevation;
    std::optional<std::string> correction_note;
};

struct PeakImportIssue {
    PeakId id;
    std::string reason;
    std::string detail;
};

struct PeakImportReport {
    std::size_t input_element_count = 0;
    std::size_t parsed_peak_count = 0;
    std::size_t selected_count = 0;
    std::size_t unselected_count = 0;
    std::size_t accepted_count = 0;
    std::size_t rejected_count = 0;
    std::size_t duplicate_count = 0;
    std::size_t missing_ele_count = 0;
    std::size_t invalid_ele_count = 0;
    std::vector<PeakImportIssue> issues;
};

struct OsmPeakImport {
    std::vector<OsmPeakNode> peaks;
    PeakImportReport report;
};

struct PeakCorrection {
    PeakId id;
    std::optional<std::string> name;
    std::optional<CatalogElevation> display_elevation;
    bool clear_display_elevation = false;
    std::string note;
};

struct PreparedPeakRecord {
    Peak peak;
    std::uint64_t osm_id = 0;
    OsmPeakNames source_names;
    std::optional<std::string> source_ele;
    std::optional<std::string> correction_note;
};

struct PreparedPeakCatalog {
    std::vector<PreparedPeakRecord> peaks;
    PeakImportReport report;
};

[[nodiscard]] Result<OsmPeakImport> parseOsmPeakNodes(const std::filesystem::path& file);
[[nodiscard]] Result<OsmPeakImport>
validateAndDeduplicatePeaks(OsmPeakImport input, const std::vector<PeakId>& selected_ids,
                            const std::vector<PeakCorrection>& corrections = {});
[[nodiscard]] Result<PreparedPeakCatalog> assignGeometryHeights(OsmPeakImport input,
                                                                const ITerrain& terrain);

[[nodiscard]] Result<FrameQuery> loadScene(const std::filesystem::path& file,
                                           const ITerrain& terrain);
[[nodiscard]] Result<Session> loadSession(const std::filesystem::path& directory,
                                          const ITerrain& terrain);
[[nodiscard]] Result<ReplayResult> replaySession(const Session& session, const ITerrain& terrain,
                                                 const PeakCatalog& catalog);
[[nodiscard]] Status writeReplayReportJson(const ReplayResult& replay, const Session& session,
                                           const RunMetadata& metadata,
                                           const std::filesystem::path& output);
[[nodiscard]] Status generateSyntheticSession(const SyntheticSessionOptions& options);
[[nodiscard]] Status writeFrameJson(const FrameResult&, const PeakCatalog&, const RunMetadata&,
                                    const std::filesystem::path& output);
[[nodiscard]] Status writeVisibilityDebugJson(const std::vector<VisibilityDebugRecord>& records,
                                              const RunMetadata& metadata,
                                              const std::filesystem::path& output);
[[nodiscard]] Status writeOverlaySvg(const FrameResult&, const PeakCatalog&, const RenderOptions&,
                                     const std::filesystem::path& output);

struct PrepareOptions {
    struct Bounds {
        double west_deg = 0.0;
        double south_deg = 0.0;
        double east_deg = 0.0;
        double north_deg = 0.0;
    };

    std::vector<std::filesystem::path> raw_dem;
    std::filesystem::path raw_osm;
    std::filesystem::path geoid_directory;
    std::filesystem::path output_directory;
    std::string geoid_model = "egm2008-2_5";
    Bounds bounds;
};
[[nodiscard]] Status prepareDataset(const PrepareOptions& options);

struct PeakPrepareOptions {
    std::filesystem::path raw_osm;
    std::filesystem::path terrain;
    std::filesystem::path selection;
    std::filesystem::path corrections;
    std::filesystem::path output_directory;
};

[[nodiscard]] Result<PeakImportReport> preparePeakCatalog(const PeakPrepareOptions& options);
} // namespace tatra::desktop
