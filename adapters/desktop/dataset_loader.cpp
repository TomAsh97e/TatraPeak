#include "file_hash.hpp"
#include "manifest_parser.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <string_view>
#include <tatra/desktop/io.hpp>
#include <tatra/validation.hpp>
#include <unordered_set>
#include <utility>

namespace tatra::desktop {
namespace {
using Json = nlohmann::json;

Error invalid(std::string message) { return {ErrorCode::InvalidInput, std::move(message)}; }

Error corrupt(std::string message) { return {ErrorCode::CorruptData, std::move(message)}; }

bool nearlyEqual(double first, double second, double scale = 1.0) {
    return std::abs(first - second) <= 1e-10 * std::max({scale, std::abs(first), std::abs(second)});
}

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
                    throw invalid("Duplicate key in " + std::string{kind} + ": " + key);
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

void exactFields(const Json& object, std::initializer_list<std::string_view> fields,
                 const std::string& path) {
    if (!object.is_object())
        throw corrupt(path + " must be an object");
    for (const auto& item : object.items()) {
        if (std::none_of(fields.begin(), fields.end(),
                         [&](std::string_view field) { return item.key() == field; }))
            throw Error{ErrorCode::UnsupportedFormat,
                        path + " contains unsupported field: " + item.key()};
    }
    for (const auto field : fields) {
        if (!object.contains(std::string{field}))
            throw corrupt(path + " is missing field: " + std::string{field});
    }
}

std::string text(const Json& object, const char* key, const std::string& path) {
    const auto& value = object.at(key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty())
        throw corrupt(path + "." + key + " must be a non-empty string");
    return value.get<std::string>();
}

double number(const Json& value, const std::string& path) {
    if (!value.is_number())
        throw corrupt(path + " must be a number");
    const double result = value.get<double>();
    if (!std::isfinite(result))
        throw corrupt(path + " must be finite");
    return result;
}

std::uint64_t positiveId(const Json& value, const std::string& path) {
    if (!value.is_number_unsigned() && !value.is_number_integer())
        throw corrupt(path + " must be a positive integer");
    if (value.is_number_unsigned()) {
        const auto result = value.get<std::uint64_t>();
        if (result == 0)
            throw corrupt(path + " must be a positive integer");
        return result;
    }
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value <= 0)
        throw corrupt(path + " must be a positive integer");
    return static_cast<std::uint64_t>(signed_value);
}

std::size_t nonNegativeSize(const Json& value, const std::string& path) {
    if (!value.is_number_unsigned() && !value.is_number_integer())
        throw corrupt(path + " must be a non-negative integer");
    std::uint64_t result = 0;
    if (value.is_number_unsigned()) {
        result = value.get<std::uint64_t>();
    } else {
        const auto signed_value = value.get<std::int64_t>();
        if (signed_value < 0)
            throw corrupt(path + " must be a non-negative integer");
        result = static_cast<std::uint64_t>(signed_value);
    }
    if (result > std::numeric_limits<std::size_t>::max())
        throw corrupt(path + " exceeds size_t");
    return static_cast<std::size_t>(result);
}

Status verifyFile(const std::filesystem::path& directory, const detail::ManifestFileEntry& entry,
                  std::string_view kind) {
    const auto path = directory / entry.path;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
        return Error{ErrorCode::IoFailure,
                     "Missing dataset " + std::string{kind} + " file: " + path.string()};
    const auto bytes = std::filesystem::file_size(path, error);
    if (error)
        return Error{ErrorCode::IoFailure, "Cannot inspect dataset file: " + path.string()};
    if (bytes != entry.bytes)
        return corrupt("Dataset " + std::string{kind} + " file size does not match manifest");
    auto hash = detail::sha256File(path);
    if (const auto* failure = std::get_if<Error>(&hash))
        return *failure;
    if (std::get<std::string>(hash) != entry.sha256)
        return corrupt("Dataset " + std::string{kind} + " SHA-256 does not match manifest");
    return std::monostate{};
}

Status validateRasterManifest(const DemGeoTiff& raster,
                              const detail::ParsedDatasetManifest& manifest) {
    if (manifest.horizontal_reference != "EPSG:4326")
        return Error{ErrorCode::UnsupportedFormat,
                     "Dataset horizontal reference must be EPSG:4326"};
    if (manifest.vertical_reference != "WGS84 ellipsoid" ||
        raster.vertical_reference != manifest.vertical_reference ||
        raster.source_vertical_reference != manifest.source_vertical_reference ||
        raster.geoid_model != manifest.geoid_model ||
        raster.geoid_sha256 != manifest.geoid_sha256 ||
        raster.normalization_formula != manifest.normalization_formula ||
        manifest.normalization_formula != "h = H + N")
        return Error{ErrorCode::UnsupportedFormat,
                     "Terrain vertical reference or conversion provenance is inconsistent"};
    if (raster.source_files.empty() || raster.source_files.size() != raster.source_sha256.size())
        return corrupt("Terrain does not record its source DEM files and hashes");
    for (std::size_t index = 0; index < raster.source_files.size(); ++index) {
        const auto present =
            std::any_of(manifest.sources.begin(), manifest.sources.end(), [&](const auto& source) {
                return std::filesystem::path{source.path}.filename() ==
                           raster.source_files[index] &&
                       source.sha256 == raster.source_sha256[index];
            });
        if (!present)
            return corrupt("Terrain source provenance is absent from manifest: " +
                           raster.source_files[index]);
    }

    const auto& geometry = raster.geometry;
    if (geometry.rows != manifest.rows || geometry.columns != manifest.columns)
        return corrupt("Terrain dimensions do not match manifest");
    for (std::size_t index = 0; index < raster.geo_transform.size(); ++index) {
        if (!nearlyEqual(raster.geo_transform[index], manifest.geo_transform[index]))
            return corrupt("Terrain geotransform does not match manifest");
    }
    if (!nearlyEqual(geometry.first_center_latitude_deg, manifest.first_center_latitude_deg) ||
        !nearlyEqual(geometry.first_center_longitude_deg, manifest.first_center_longitude_deg) ||
        !nearlyEqual(geometry.latitude_step_deg, manifest.latitude_step_deg) ||
        !nearlyEqual(geometry.longitude_step_deg, manifest.longitude_step_deg))
        return corrupt("Terrain grid geometry does not match manifest");
    if (raster.no_data_value.has_value() != manifest.no_data_value.has_value() ||
        (raster.no_data_value && !nearlyEqual(*raster.no_data_value, *manifest.no_data_value)))
        return corrupt("Terrain NoData does not match manifest");
    if (raster.area_or_point != manifest.area_or_point || raster.area_or_point != "Point")
        return corrupt("Terrain AREA_OR_POINT does not match manifest");
    const std::array<double, 4> coverage{
        raster.geo_transform[0],
        raster.geo_transform[3] + static_cast<double>(geometry.rows) * raster.geo_transform[5],
        raster.geo_transform[0] + static_cast<double>(geometry.columns) * raster.geo_transform[1],
        raster.geo_transform[3]};
    for (std::size_t index = 0; index < coverage.size(); ++index) {
        if (!nearlyEqual(coverage[index], manifest.coverage_edges_wsen[index]))
            return corrupt("Terrain coverage does not match manifest");
    }
    if (manifest.resampling != "none (natural source grid)")
        return Error{ErrorCode::UnsupportedFormat, "Dataset must preserve the natural DEM grid"};
    return std::monostate{};
}

Result<PeakCatalog> readPeakCatalog(const std::filesystem::path& file,
                                    const detail::ParsedDatasetManifest& manifest,
                                    const GridTerrain& terrain) {
    auto loaded = readJson(file, "peak catalog");
    if (const auto* error = std::get_if<Error>(&loaded))
        return *error;
    try {
        const auto& document = std::get<Json>(loaded);
        exactFields(document, {"schema_version", "tool_version", "source", "report", "peaks"},
                    "peaks");
        if (!document.at("schema_version").is_number_integer() ||
            document.at("schema_version") != 1)
            return Error{ErrorCode::UnsupportedFormat,
                         "Only peak catalog schema_version=1 is supported"};
        static_cast<void>(text(document, "tool_version", "peaks"));
        const auto& source = document.at("source");
        exactFields(source,
                    {"osm_file", "osm_sha256", "terrain_file", "terrain_sha256", "selection_file",
                     "selection_sha256", "corrections_file", "corrections_sha256"},
                    "peaks.source");
        if (text(source, "terrain_file", "peaks.source") != manifest.terrain_file.path ||
            text(source, "terrain_sha256", "peaks.source") != manifest.terrain_file.sha256)
            return corrupt("Peak catalog references a different terrain file");

        const auto& report = document.at("report");
        exactFields(report,
                    {"input_element_count", "parsed_peak_count", "selected_count",
                     "unselected_count", "accepted_count", "rejected_count", "duplicate_count",
                     "missing_ele_count", "invalid_ele_count", "reason_counts", "issues"},
                    "peaks.report");
        const auto accepted_count =
            nonNegativeSize(report.at("accepted_count"), "peaks.report.accepted_count");

        const auto& records = document.at("peaks");
        if (!records.is_array())
            return corrupt("peaks.peaks must be an array");
        if (records.size() != manifest.peak_count || accepted_count != records.size())
            return corrupt("Peak count does not match manifest or import report");
        PeakCatalog catalog;
        catalog.peaks.reserve(records.size());
        std::unordered_set<PeakId> ids;
        bool has_dem = false;
        bool has_explicit = false;
        for (std::size_t index = 0; index < records.size(); ++index) {
            const auto path = "peaks.peaks[" + std::to_string(index) + "]";
            const auto& record = records[index];
            if (record.contains("geometry_height_correction"))
                exactFields(record,
                            {"id", "name", "geometry_position", "geometry_height_source",
                             "display_elevation", "source", "correction_note",
                             "geometry_height_correction"},
                            path);
            else
                exactFields(record,
                            {"id", "name", "geometry_position", "geometry_height_source",
                             "display_elevation", "source", "correction_note"},
                            path);
            Peak peak;
            peak.id = text(record, "id", path);
            peak.name = text(record, "name", path);
            if (!ids.insert(peak.id).second)
                return corrupt("Duplicate peak id: " + peak.id);
            const auto height_source = text(record, "geometry_height_source", path);
            const bool explicit_height = height_source == "ExplicitEllipsoid";
            if (height_source != "DemSample" && !explicit_height)
                return Error{ErrorCode::UnsupportedFormat,
                             "Unsupported peak geometry height source"};
            has_dem |= !explicit_height;
            has_explicit |= explicit_height;
            const auto& position = record.at("geometry_position");
            exactFields(position, {"latitude_deg", "longitude_deg", "ellipsoid_height_m"},
                        path + ".geometry_position");
            const auto coord_result =
                makeGeoCoord(number(position.at("latitude_deg"), path + ".latitude_deg"),
                             number(position.at("longitude_deg"), path + ".longitude_deg"));
            if (const auto* error = std::get_if<Error>(&coord_result))
                return corrupt(path + ": " + error->message);
            const auto coord = std::get<GeoCoord>(coord_result);
            const double ellipsoid_height =
                number(position.at("ellipsoid_height_m"), path + ".ellipsoid_height_m");
            const auto position_result = makeGeoPosition(coord, ellipsoid_height);
            if (const auto* error = std::get_if<Error>(&position_result))
                return corrupt(path + ": " + error->message);
            peak.geometry_position = std::get<GeoPosition>(position_result);
            peak.geometry_height_source = explicit_height ? GeometryHeightSource::ExplicitEllipsoid
                                                          : GeometryHeightSource::DemSample;
            if (explicit_height) {
                const auto& correction = record.at("geometry_height_correction");
                exactFields(correction, {"ellipsoid_height_m", "source"},
                            path + ".geometry_height_correction");
                (void)text(correction, "source", path + ".geometry_height_correction");
                (void)text(record, "correction_note", path);
                if (number(correction.at("ellipsoid_height_m"), path) != ellipsoid_height)
                    return corrupt("Geometry correction differs from peak height: " + peak.id);
            } else if (record.contains("geometry_height_correction")) {
                return corrupt("DEM peak cannot declare a geometry correction: " + peak.id);
            }

            const auto sampled = terrain.sampleHeight(coord);
            const auto* height = std::get_if<HeightSample>(&sampled);
            if (!height)
                return corrupt("Peak is outside valid terrain coverage: " + peak.id);
            if (!explicit_height && !nearlyEqual(height->ellipsoid_height_m, ellipsoid_height, 1.0))
                return corrupt("Peak geometry height does not match terrain: " + peak.id);

            const auto& elevation = record.at("display_elevation");
            if (!elevation.is_null()) {
                exactFields(elevation, {"metres", "vertical_reference", "source"},
                            path + ".display_elevation");
                peak.display_elevation = CatalogElevation{
                    number(elevation.at("metres"), path + ".display_elevation"),
                    text(elevation, "vertical_reference", path + ".display_elevation"),
                    text(elevation, "source", path + ".display_elevation")};
            }

            const auto& osm = record.at("source");
            exactFields(osm, {"osm_type", "osm_id", "names", "ele"}, path + ".source");
            if (text(osm, "osm_type", path + ".source") != "node")
                return Error{ErrorCode::UnsupportedFormat, "Only OSM nodes are supported"};
            const auto osm_id = positiveId(osm.at("osm_id"), path + ".source.osm_id");
            if (peak.id != "osm:node:" + std::to_string(osm_id))
                return corrupt("Peak stable id does not match source OSM id: " + peak.id);
            const auto& names = osm.at("names");
            exactFields(names, {"name", "name:pl", "name:sk"}, path + ".source.names");
            for (const auto key : {"name", "name:pl", "name:sk"}) {
                if (!names.at(key).is_null() && !names.at(key).is_string())
                    return corrupt(path + ".source.names values must be strings or null");
            }
            if (!osm.at("ele").is_null() && !osm.at("ele").is_string())
                return corrupt(path + ".source.ele must be a string or null");
            if (!record.at("correction_note").is_null() &&
                !record.at("correction_note").is_string())
                return corrupt(path + ".correction_note must be a string or null");
            catalog.peaks.push_back(std::move(peak));
        }
        const std::string expected_source =
            has_explicit ? (has_dem ? "Mixed" : "ExplicitEllipsoid") : "DemSample";
        if (manifest.geometry_height_source != expected_source)
            return corrupt("Manifest geometry height source does not match catalog");
        return catalog;
    } catch (const Error& error) {
        return error;
    } catch (const Json::exception& error) {
        return corrupt("Invalid peak catalog fields: " + std::string{error.what()});
    }
}
} // namespace

Status validateDataset(const Dataset& dataset) {
    const auto& metadata = dataset.metadata;
    if (metadata.schema_version != 1 || metadata.dataset_id.empty() ||
        metadata.manifest_sha256.empty() || metadata.horizontal_reference != "EPSG:4326" ||
        metadata.vertical_reference != "WGS84 ellipsoid" ||
        metadata.source_vertical_reference.empty() || metadata.geoid_model.empty() ||
        metadata.geoid_sha256.empty() || metadata.terrain_rows < 2 ||
        metadata.terrain_columns < 2 || metadata.peak_count != dataset.catalog.peaks.size())
        return corrupt("Dataset metadata is incomplete or inconsistent");
    std::unordered_set<PeakId> ids;
    for (const auto& peak : dataset.catalog.peaks) {
        if (peak.id.empty() || peak.name.empty() || !ids.insert(peak.id).second)
            return corrupt("Dataset catalog has an empty or duplicate peak id");
        const auto position = makeGeoPosition(peak.geometry_position.coord,
                                              peak.geometry_position.ellipsoid_height_m);
        if (std::holds_alternative<Error>(position))
            return corrupt("Dataset catalog has an invalid peak position: " + peak.id);
        if (peak.geometry_height_source != GeometryHeightSource::DemSample &&
            peak.geometry_height_source != GeometryHeightSource::ExplicitEllipsoid)
            return corrupt("Unsupported dataset peak height source: " + peak.id);
        const auto sampled = dataset.terrain.sampleHeight(peak.geometry_position.coord);
        const auto* height = std::get_if<HeightSample>(&sampled);
        if (!height || (peak.geometry_height_source == GeometryHeightSource::DemSample &&
                        !nearlyEqual(height->ellipsoid_height_m,
                                     peak.geometry_position.ellipsoid_height_m, 1.0)))
            return corrupt("Dataset peak is outside terrain or has a mismatched height: " +
                           peak.id);
        if (peak.display_elevation && (!std::isfinite(peak.display_elevation->metres) ||
                                       peak.display_elevation->vertical_reference.empty() ||
                                       peak.display_elevation->source.empty()))
            return corrupt("Dataset peak display elevation is invalid: " + peak.id);
    }
    return std::monostate{};
}

Result<Dataset> loadDataset(const std::filesystem::path& directory) {
    const auto manifest_path = directory / "manifest.json";
    auto manifest_result = detail::parseDatasetManifest(manifest_path);
    if (const auto* error = std::get_if<Error>(&manifest_result))
        return *error;
    const auto manifest = std::get<detail::ParsedDatasetManifest>(manifest_result);
    if (manifest.terrain_file.path != "terrain-ellipsoid.tif" ||
        manifest.peaks_file.path != "peaks.json" ||
        manifest.attribution_file.path != "ATTRIBUTION.txt")
        return Error{ErrorCode::UnsupportedFormat, "Dataset uses unsupported package filenames"};
    for (const auto& [entry, kind] :
         {std::pair{&manifest.terrain_file, std::string_view{"terrain"}},
          std::pair{&manifest.peaks_file, std::string_view{"peaks"}},
          std::pair{&manifest.attribution_file, std::string_view{"attribution"}}}) {
        const auto verified = verifyFile(directory, *entry, kind);
        if (const auto* error = std::get_if<Error>(&verified))
            return *error;
    }
    std::ifstream attribution(directory / manifest.attribution_file.path, std::ios::binary);
    const std::string attribution_text{std::istreambuf_iterator<char>{attribution},
                                       std::istreambuf_iterator<char>{}};
    for (const auto& required : manifest.required_attribution) {
        if (attribution_text.find(required) == std::string::npos)
            return corrupt("Attribution file is missing required notice: " + required);
    }

    auto raster_result = readDemGeoTiff(directory / manifest.terrain_file.path);
    if (const auto* error = std::get_if<Error>(&raster_result))
        return *error;
    auto raster = std::get<DemGeoTiff>(std::move(raster_result));
    const auto raster_valid = validateRasterManifest(raster, manifest);
    if (const auto* error = std::get_if<Error>(&raster_valid))
        return *error;
    auto terrain_result = GridTerrain::create(raster.geometry, std::move(raster.height_values_m),
                                              std::move(raster.valid_mask));
    if (const auto* error = std::get_if<Error>(&terrain_result))
        return *error;
    auto terrain = std::get<GridTerrain>(std::move(terrain_result));
    auto catalog_result = readPeakCatalog(directory / manifest.peaks_file.path, manifest, terrain);
    if (const auto* error = std::get_if<Error>(&catalog_result))
        return *error;
    auto manifest_hash = detail::sha256File(manifest_path);
    if (const auto* error = std::get_if<Error>(&manifest_hash))
        return *error;
    DatasetMetadata metadata{manifest.schema_version,
                             manifest.dataset_id,
                             std::get<std::string>(std::move(manifest_hash)),
                             manifest.horizontal_reference,
                             manifest.vertical_reference,
                             manifest.source_vertical_reference,
                             manifest.geoid_model,
                             manifest.geoid_sha256,
                             manifest.rows,
                             manifest.columns,
                             manifest.peak_count};
    Dataset dataset{std::move(metadata), std::get<PeakCatalog>(std::move(catalog_result)),
                    std::move(terrain)};
    const auto valid = validateDataset(dataset);
    if (const auto* error = std::get_if<Error>(&valid))
        return *error;
    return dataset;
}
} // namespace tatra::desktop
