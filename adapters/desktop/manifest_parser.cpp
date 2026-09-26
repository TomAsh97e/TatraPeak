#include "manifest_parser.hpp"

#include "file_hash.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <string_view>
#include <tatra/version.hpp>
#include <utility>

namespace tatra::desktop {
namespace {
using Json = nlohmann::json;

Error invalid(std::string message) { return {ErrorCode::InvalidInput, std::move(message)}; }

Error corrupt(std::string message) { return {ErrorCode::CorruptData, std::move(message)}; }

Result<Json> readJson(const std::filesystem::path& file, std::string_view kind) {
    try {
        std::ifstream input(file, std::ios::binary);
        if (!input)
            return Error{ErrorCode::IoFailure,
                         "Cannot open " + std::string{kind} + ": " + file.string()};
        input.exceptions(std::ios::badbit);
        const std::string contents{std::istreambuf_iterator<char>{input},
                                   std::istreambuf_iterator<char>{}};
        std::vector<std::vector<std::string>> object_keys;
        return Json::parse(contents, [&](int, Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start)
                object_keys.emplace_back();
            if (event == Json::parse_event_t::key) {
                const auto key = parsed.get<std::string>();
                if (std::find(object_keys.back().begin(), object_keys.back().end(), key) !=
                    object_keys.back().end())
                    throw Error{ErrorCode::InvalidInput,
                                "Duplicate key in " + std::string{kind} + ": " + key};
                object_keys.back().push_back(key);
            }
            if (event == Json::parse_event_t::object_end)
                object_keys.pop_back();
            return true;
        });
    } catch (const Error& error) {
        return error;
    } catch (const Json::exception& error) {
        return invalid("Invalid " + std::string{kind} + " JSON: " + error.what());
    } catch (const std::ios_base::failure& error) {
        return Error{ErrorCode::IoFailure,
                     "Cannot read " + std::string{kind} + ": " + error.what()};
    }
}

bool validSha256(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool validDatasetId(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '-' || character == '_' ||
               character == '.';
    });
}

bool samePath(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(first, second, error);
    return !error && equivalent;
}

std::uint64_t fileSize(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > std::numeric_limits<std::uint64_t>::max())
        throw Error{ErrorCode::IoFailure, "Cannot determine file size: " + path.string()};
    return static_cast<std::uint64_t>(size);
}

std::string getHash(Result<std::string> result) {
    if (const auto* error = std::get_if<Error>(&result))
        throw *error;
    return std::get<std::string>(std::move(result));
}

void writeText(const std::filesystem::path& output, const std::string& contents) {
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(output, std::ios::binary | std::ios::trunc);
    stream << contents;
    stream.close();
}

Json fileEntry(const std::filesystem::path& path, const std::string& sha256) {
    return {{"path", path.filename().string()}, {"sha256", sha256}, {"bytes", fileSize(path)}};
}

bool containsSource(const Json& sources, const std::string& filename, const std::string& sha256) {
    return std::any_of(sources.begin(), sources.end(), [&](const Json& source) {
        return source.is_object() && source.contains("path") && source["path"].is_string() &&
               source.contains("sha256") && source["sha256"].is_string() &&
               std::filesystem::path{source["path"].get<std::string>()}.filename() == filename &&
               source["sha256"].get<std::string>() == sha256;
    });
}

Result<std::size_t> peakCountAndTerrainHash(const std::filesystem::path& path,
                                            const std::string& terrain_hash) {
    auto loaded = readJson(path, "peak catalog");
    if (const auto* error = std::get_if<Error>(&loaded))
        return *error;
    const auto& document = std::get<Json>(loaded);
    if (!document.is_object() || !document.contains("schema_version") ||
        document["schema_version"] != 1 || !document.contains("peaks") ||
        !document["peaks"].is_array() || !document.contains("source") ||
        !document["source"].is_object() || !document["source"].contains("terrain_sha256") ||
        !document["source"]["terrain_sha256"].is_string())
        return invalid("Peak catalog does not satisfy the T12 schema");
    if (document["source"]["terrain_sha256"].get<std::string>() != terrain_hash)
        return corrupt("Peak catalog was prepared from a different terrain file");
    return document["peaks"].size();
}

} // namespace

namespace detail {
namespace {
void exactFields(const Json& object, std::initializer_list<std::string_view> fields,
                 const std::string& path) {
    if (!object.is_object())
        throw invalid(path + " must be an object");
    for (const auto& item : object.items()) {
        if (std::none_of(fields.begin(), fields.end(),
                         [&](std::string_view field) { return item.key() == field; }))
            throw Error{ErrorCode::UnsupportedFormat,
                        path + " contains unsupported field: " + item.key()};
    }
    for (const auto field : fields) {
        if (!object.contains(std::string{field}))
            throw invalid(path + " is missing field: " + std::string{field});
    }
}

std::string text(const Json& object, const char* key, const std::string& path) {
    const auto& value = object.at(key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty())
        throw invalid(path + "." + key + " must be a non-empty string");
    return value.get<std::string>();
}

double number(const Json& value, const std::string& path) {
    if (!value.is_number())
        throw invalid(path + " must be a number");
    const double result = value.get<double>();
    if (!std::isfinite(result))
        throw invalid(path + " must be finite");
    return result;
}

std::size_t sizeValue(const Json& value, const std::string& path) {
    if (!value.is_number_unsigned() && !value.is_number_integer())
        throw invalid(path + " must be a non-negative integer");
    std::uint64_t result = 0;
    if (value.is_number_unsigned()) {
        result = value.get<std::uint64_t>();
    } else {
        const auto signed_result = value.get<std::int64_t>();
        if (signed_result < 0)
            throw invalid(path + " must be a non-negative integer");
        result = static_cast<std::uint64_t>(signed_result);
    }
    if (result > std::numeric_limits<std::size_t>::max())
        throw invalid(path + " exceeds size_t");
    return static_cast<std::size_t>(result);
}

ManifestFileEntry parseFile(const Json& value, const std::string& path) {
    exactFields(value, {"path", "sha256", "bytes"}, path);
    ManifestFileEntry result;
    result.path = text(value, "path", path);
    const std::filesystem::path relative{result.path};
    if (relative.is_absolute() || relative.has_parent_path() || relative.filename() != relative)
        throw invalid(path + ".path must be a plain filename");
    result.sha256 = text(value, "sha256", path);
    if (!validSha256(result.sha256))
        throw invalid(path + ".sha256 is not a lowercase SHA-256");
    result.bytes = static_cast<std::uint64_t>(sizeValue(value.at("bytes"), path + ".bytes"));
    return result;
}
} // namespace

Result<ParsedDatasetManifest> parseDatasetManifest(const std::filesystem::path& file) {
    auto loaded = readJson(file, "dataset manifest");
    if (const auto* error = std::get_if<Error>(&loaded))
        return *error;
    try {
        const auto& document = std::get<Json>(loaded);
        exactFields(document,
                    {"schema_version", "dataset_id", "retrieved_at", "tool", "source_metadata",
                     "files", "spatial_reference", "raster", "catalog", "required_attribution",
                     "sources"},
                    "manifest");
        if (!document.at("schema_version").is_number_integer() ||
            document.at("schema_version") != 1)
            return Error{ErrorCode::UnsupportedFormat,
                         "Only dataset manifest schema_version=1 is supported"};
        ParsedDatasetManifest result;
        result.schema_version = 1;
        result.dataset_id = text(document, "dataset_id", "manifest");
        if (!validDatasetId(result.dataset_id))
            return invalid("manifest.dataset_id contains unsupported characters");
        result.retrieved_at = text(document, "retrieved_at", "manifest");

        const auto& tool = document.at("tool");
        exactFields(tool, {"name", "version"}, "manifest.tool");
        result.tool_name = text(tool, "name", "manifest.tool");
        result.tool_version = text(tool, "version", "manifest.tool");

        const auto& source_metadata = document.at("source_metadata");
        exactFields(source_metadata,
                    {"path", "sha256", "dem_working_bbox_wsen", "osm_query_bbox_swne"},
                    "manifest.source_metadata");
        static_cast<void>(text(source_metadata, "path", "manifest.source_metadata"));
        result.source_metadata_sha256 = text(source_metadata, "sha256", "manifest.source_metadata");
        if (!validSha256(result.source_metadata_sha256))
            return invalid("manifest.source_metadata.sha256 is invalid");
        for (const auto key : {"dem_working_bbox_wsen", "osm_query_bbox_swne"}) {
            const auto& bounds = source_metadata.at(key);
            if (!bounds.is_array() || bounds.size() != 4)
                return invalid("manifest.source_metadata bounds must have four values");
            for (std::size_t index = 0; index < bounds.size(); ++index)
                static_cast<void>(number(bounds[index], "manifest.source_metadata bounds"));
        }

        const auto& files = document.at("files");
        exactFields(files, {"terrain", "peaks", "attribution"}, "manifest.files");
        result.terrain_file = parseFile(files.at("terrain"), "manifest.files.terrain");
        result.peaks_file = parseFile(files.at("peaks"), "manifest.files.peaks");
        result.attribution_file = parseFile(files.at("attribution"), "manifest.files.attribution");

        const auto& spatial = document.at("spatial_reference");
        exactFields(spatial,
                    {"horizontal", "source_vertical", "result_vertical", "geoid_model",
                     "geoid_sha256", "normalization_formula"},
                    "manifest.spatial_reference");
        result.horizontal_reference = text(spatial, "horizontal", "manifest.spatial_reference");
        result.source_vertical_reference =
            text(spatial, "source_vertical", "manifest.spatial_reference");
        result.vertical_reference = text(spatial, "result_vertical", "manifest.spatial_reference");
        result.geoid_model = text(spatial, "geoid_model", "manifest.spatial_reference");
        result.geoid_sha256 = text(spatial, "geoid_sha256", "manifest.spatial_reference");
        if (!validSha256(result.geoid_sha256))
            return invalid("manifest.spatial_reference.geoid_sha256 is invalid");
        result.normalization_formula =
            text(spatial, "normalization_formula", "manifest.spatial_reference");

        const auto& raster = document.at("raster");
        exactFields(raster,
                    {"rows", "columns", "geo_transform", "first_center_latitude_deg",
                     "first_center_longitude_deg", "latitude_step_deg", "longitude_step_deg",
                     "no_data", "area_or_point", "coverage_edges_wsen", "resampling"},
                    "manifest.raster");
        result.rows = sizeValue(raster.at("rows"), "manifest.raster.rows");
        result.columns = sizeValue(raster.at("columns"), "manifest.raster.columns");
        const auto& transform = raster.at("geo_transform");
        if (!transform.is_array() || transform.size() != result.geo_transform.size())
            return invalid("manifest.raster.geo_transform must have six values");
        for (std::size_t index = 0; index < transform.size(); ++index)
            result.geo_transform[index] = number(transform[index], "manifest.raster.geo_transform");
        result.first_center_latitude_deg = number(raster.at("first_center_latitude_deg"),
                                                  "manifest.raster.first_center_latitude_deg");
        result.first_center_longitude_deg = number(raster.at("first_center_longitude_deg"),
                                                   "manifest.raster.first_center_longitude_deg");
        result.latitude_step_deg =
            number(raster.at("latitude_step_deg"), "manifest.raster.latitude_step_deg");
        result.longitude_step_deg =
            number(raster.at("longitude_step_deg"), "manifest.raster.longitude_step_deg");
        if (!raster.at("no_data").is_null())
            result.no_data_value = number(raster.at("no_data"), "manifest.raster.no_data");
        result.area_or_point = text(raster, "area_or_point", "manifest.raster");
        const auto& coverage = raster.at("coverage_edges_wsen");
        if (!coverage.is_array() || coverage.size() != result.coverage_edges_wsen.size())
            return invalid("manifest.raster.coverage_edges_wsen must have four values");
        for (std::size_t index = 0; index < coverage.size(); ++index)
            result.coverage_edges_wsen[index] =
                number(coverage[index], "manifest.raster.coverage_edges_wsen");
        result.resampling = text(raster, "resampling", "manifest.raster");

        const auto& catalog = document.at("catalog");
        exactFields(catalog, {"peak_count", "geometry_height_source"}, "manifest.catalog");
        result.peak_count = sizeValue(catalog.at("peak_count"), "manifest.catalog.peak_count");
        result.geometry_height_source = text(catalog, "geometry_height_source", "manifest.catalog");

        const auto& attribution = document.at("required_attribution");
        if (!attribution.is_array() || attribution.empty())
            return invalid("manifest.required_attribution must be a non-empty array");
        for (const auto& value : attribution) {
            if (!value.is_string() || value.get_ref<const std::string&>().empty())
                return invalid("manifest.required_attribution values must be non-empty strings");
            result.required_attribution.push_back(value.get<std::string>());
        }

        const auto& sources = document.at("sources");
        if (!sources.is_array() || sources.empty())
            return invalid("manifest.sources must be a non-empty array");
        for (const auto& source : sources) {
            if (!source.is_object())
                return invalid("manifest source must be an object");
            ManifestSourceEntry entry;
            entry.path = text(source, "path", "manifest.sources[]");
            entry.sha256 = text(source, "sha256", "manifest.sources[]");
            if (!validSha256(entry.sha256))
                return invalid("manifest source SHA-256 is invalid");
            static_cast<void>(text(source, "source", "manifest.sources[]"));
            static_cast<void>(text(source, "dataset", "manifest.sources[]"));
            result.sources.push_back(std::move(entry));
        }
        return result;
    } catch (const Error& error) {
        return error;
    } catch (const Json::exception& error) {
        return invalid("Invalid dataset manifest fields: " + std::string{error.what()});
    }
}
} // namespace detail

Status writeDatasetManifest(const DatasetManifestOptions& options) {
    try {
        if (!validDatasetId(options.dataset_id))
            return invalid("dataset-id must contain only letters, digits, '.', '_' or '-'");
        if (options.terrain.empty() || options.peaks.empty() || options.source_metadata.empty() ||
            options.attribution.empty() || options.output_directory.empty())
            return invalid("All manifest input and output paths are required");
        const auto expected_terrain = options.output_directory / "terrain-ellipsoid.tif";
        const auto expected_peaks = options.output_directory / "peaks.json";
        if (!samePath(options.terrain, expected_terrain) ||
            !samePath(options.peaks, expected_peaks))
            return invalid("Terrain and peaks must already be in the output dataset directory");

        auto raster_result = readDemGeoTiff(options.terrain);
        if (const auto* error = std::get_if<Error>(&raster_result))
            return *error;
        const auto& raster = std::get<DemGeoTiff>(raster_result);
        if (raster.vertical_reference != "WGS84 ellipsoid" ||
            raster.source_vertical_reference.empty() || raster.geoid_model.empty() ||
            !validSha256(raster.geoid_sha256) || raster.normalization_formula != "h = H + N" ||
            raster.area_or_point != "Point")
            return Error{ErrorCode::UnsupportedFormat,
                         "Terrain lacks the explicit T11 ellipsoid conversion provenance"};
        if (raster.source_files.empty() ||
            raster.source_files.size() != raster.source_sha256.size())
            return corrupt("Terrain source DEM provenance is incomplete");

        const auto terrain_hash = getHash(detail::sha256File(options.terrain));
        auto peak_count_result = peakCountAndTerrainHash(options.peaks, terrain_hash);
        if (const auto* error = std::get_if<Error>(&peak_count_result))
            return *error;
        const auto peak_count = std::get<std::size_t>(peak_count_result);
        const auto peaks_hash = getHash(detail::sha256File(options.peaks));

        auto source_result = readJson(options.source_metadata, "source metadata");
        if (const auto* error = std::get_if<Error>(&source_result))
            return *error;
        const auto& source = std::get<Json>(source_result);
        if (!source.is_object() || !source.contains("schema_version") ||
            source["schema_version"] != 1 || !source.contains("retrieved_at") ||
            !source["retrieved_at"].is_string() || !source.contains("dem_working_bbox_wsen") ||
            !source["dem_working_bbox_wsen"].is_array() ||
            source["dem_working_bbox_wsen"].size() != 4 ||
            !source.contains("osm_query_bbox_swne") || !source["osm_query_bbox_swne"].is_array() ||
            source["osm_query_bbox_swne"].size() != 4 || !source.contains("sources") ||
            !source["sources"].is_array() || source["sources"].empty())
            return invalid("Source metadata does not satisfy the T10 schema");
        for (std::size_t index = 0; index < raster.source_files.size(); ++index) {
            if (!containsSource(source["sources"], raster.source_files[index],
                                raster.source_sha256[index]))
                return corrupt("Terrain source hash is absent from T10 metadata: " +
                               raster.source_files[index]);
        }

        std::ifstream attribution_stream(options.attribution, std::ios::binary);
        if (!attribution_stream)
            return Error{ErrorCode::IoFailure,
                         "Cannot open attribution: " + options.attribution.string()};
        const std::string attribution{std::istreambuf_iterator<char>{attribution_stream},
                                      std::istreambuf_iterator<char>{}};
        if (attribution.empty())
            return invalid("Attribution file must not be empty");
        std::error_code directory_error;
        std::filesystem::create_directories(options.output_directory, directory_error);
        if (directory_error)
            return Error{ErrorCode::IoFailure,
                         "Cannot create dataset directory: " + directory_error.message()};
        const auto output_attribution = options.output_directory / "ATTRIBUTION.txt";
        if (!samePath(options.attribution, output_attribution))
            writeText(output_attribution, attribution);

        const auto source_metadata_hash = getHash(detail::sha256File(options.source_metadata));
        const auto attribution_hash = getHash(detail::sha256File(output_attribution));
        const auto& geometry = raster.geometry;
        const double west = raster.geo_transform[0];
        const double north = raster.geo_transform[3];
        const double east = west + static_cast<double>(geometry.columns) * raster.geo_transform[1];
        const double south = north + static_cast<double>(geometry.rows) * raster.geo_transform[5];
        Json no_data = raster.no_data_value ? Json(*raster.no_data_value) : Json(nullptr);
        Json manifest = {
            {"schema_version", 1},
            {"dataset_id", options.dataset_id},
            {"retrieved_at", source["retrieved_at"]},
            {"tool", {{"name", "tatra-prepare"}, {"version", tatra::version}}},
            {"source_metadata",
             {{"path", options.source_metadata.filename().string()},
              {"sha256", source_metadata_hash},
              {"dem_working_bbox_wsen", source["dem_working_bbox_wsen"]},
              {"osm_query_bbox_swne", source["osm_query_bbox_swne"]}}},
            {"files",
             {{"terrain", fileEntry(options.terrain, terrain_hash)},
              {"peaks", fileEntry(options.peaks, peaks_hash)},
              {"attribution", fileEntry(output_attribution, attribution_hash)}}},
            {"spatial_reference",
             {{"horizontal", "EPSG:4326"},
              {"source_vertical", raster.source_vertical_reference},
              {"result_vertical", raster.vertical_reference},
              {"geoid_model", raster.geoid_model},
              {"geoid_sha256", raster.geoid_sha256},
              {"normalization_formula", raster.normalization_formula}}},
            {"raster",
             {{"rows", geometry.rows},
              {"columns", geometry.columns},
              {"geo_transform", raster.geo_transform},
              {"first_center_latitude_deg", geometry.first_center_latitude_deg},
              {"first_center_longitude_deg", geometry.first_center_longitude_deg},
              {"latitude_step_deg", geometry.latitude_step_deg},
              {"longitude_step_deg", geometry.longitude_step_deg},
              {"no_data", std::move(no_data)},
              {"area_or_point", raster.area_or_point},
              {"coverage_edges_wsen", {west, south, east, north}},
              {"resampling", "none (natural source grid)"}}},
            {"catalog", {{"peak_count", peak_count}, {"geometry_height_source", "DemSample"}}},
            {"required_attribution",
             {"Copernicus DEM GLO-30 Public", "GeographicLib geoid dataset egm2008-2_5",
              "OpenStreetMap"}},
            {"sources", source["sources"]}};
        const auto serialized = manifest.dump(2) + '\n';
        writeText(options.output_directory / "manifest.json", serialized);

        auto loaded_dataset = loadDataset(options.output_directory);
        if (const auto* error = std::get_if<Error>(&loaded_dataset))
            return *error;
        return std::monostate{};
    } catch (const Error& error) {
        return error;
    } catch (const Json::exception& error) {
        return invalid("Cannot create dataset manifest: " + std::string{error.what()});
    } catch (const std::ios_base::failure& error) {
        return Error{ErrorCode::IoFailure,
                     "Cannot write dataset manifest: " + std::string{error.what()}};
    }
}
} // namespace tatra::desktop
