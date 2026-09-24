#pragma once
#include <tatra/engine.hpp>
#include <tatra/pose_timeline.hpp>
#include <filesystem>

namespace tatra::desktop {
struct DatasetMetadata {
    int schema_version = 1;
    std::string dataset_id;
    std::string manifest_sha256;
    std::string horizontal_reference;
    std::string vertical_reference;
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
};

struct RenderOptions {
    CameraIntrinsics camera;
    bool debug_visibility = false;
    std::optional<std::filesystem::path> background_image;
};

[[nodiscard]] Result<Dataset> loadDataset(const std::filesystem::path& directory);
[[nodiscard]] Result<GridTerrain> loadNormalizedGeoTiff(const std::filesystem::path& file);
[[nodiscard]] Result<FrameQuery> loadScene(const std::filesystem::path& file, const ITerrain& terrain);
[[nodiscard]] Result<Session> loadSession(const std::filesystem::path& directory, const ITerrain& terrain);
[[nodiscard]] Status writeFrameJson(const FrameResult&, const PeakCatalog&,
    const RunMetadata&, const std::filesystem::path& output);
[[nodiscard]] Status writeOverlaySvg(const FrameResult&, const PeakCatalog&,
    const RenderOptions&, const std::filesystem::path& output);

struct PrepareOptions {
    std::filesystem::path raw_dem;
    std::filesystem::path raw_osm;
    std::filesystem::path geoid_directory;
    std::filesystem::path output_directory;
    std::string geoid_model = "egm2008-2_5";
};
[[nodiscard]] Status prepareDataset(const PrepareOptions& options);
} // namespace tatra::desktop
