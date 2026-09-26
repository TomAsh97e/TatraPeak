#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <tatra/desktop/io.hpp>
#include <vector>

namespace tatra::desktop::detail {
struct ManifestFileEntry {
    std::string path;
    std::string sha256;
    std::uint64_t bytes = 0;
};

struct ManifestSourceEntry {
    std::string path;
    std::string sha256;
};

struct ParsedDatasetManifest {
    int schema_version = 0;
    std::string dataset_id;
    std::string retrieved_at;
    std::string tool_name;
    std::string tool_version;
    std::string source_metadata_sha256;
    ManifestFileEntry terrain_file;
    ManifestFileEntry peaks_file;
    ManifestFileEntry attribution_file;
    std::string horizontal_reference;
    std::string source_vertical_reference;
    std::string vertical_reference;
    std::string geoid_model;
    std::string geoid_sha256;
    std::string normalization_formula;
    std::size_t rows = 0;
    std::size_t columns = 0;
    std::array<double, 6> geo_transform{};
    double first_center_latitude_deg = 0.0;
    double first_center_longitude_deg = 0.0;
    double latitude_step_deg = 0.0;
    double longitude_step_deg = 0.0;
    std::optional<double> no_data_value;
    std::string area_or_point;
    std::array<double, 4> coverage_edges_wsen{};
    std::string resampling;
    std::size_t peak_count = 0;
    std::string geometry_height_source;
    std::vector<std::string> required_attribution;
    std::vector<ManifestSourceEntry> sources;
};

[[nodiscard]] Result<ParsedDatasetManifest> parseDatasetManifest(const std::filesystem::path& file);
} // namespace tatra::desktop::detail
