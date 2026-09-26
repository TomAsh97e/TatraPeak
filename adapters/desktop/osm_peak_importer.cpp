#include "file_hash.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <string_view>
#include <tatra/desktop/io.hpp>
#include <tatra/version.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tatra::desktop {
namespace {
using Json = nlohmann::json;

struct PeakProvenance {
    std::filesystem::path raw_osm;
    std::filesystem::path terrain;
    std::filesystem::path selection;
    std::filesystem::path corrections;
    std::string raw_osm_sha256;
    std::string terrain_sha256;
    std::string selection_sha256;
    std::string corrections_sha256;
};

Error invalid(std::string message) { return {ErrorCode::InvalidInput, std::move(message)}; }

void addIssue(PeakImportReport& report, PeakId id, std::string reason, std::string detail) {
    report.issues.push_back({std::move(id), std::move(reason), std::move(detail)});
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
                    throw Error{ErrorCode::InvalidInput,
                                "Duplicate JSON key in " + std::string{kind} + ": " + key};
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

std::optional<std::string> stringTag(const Json& tags, const char* key) {
    const auto found = tags.find(key);
    if (found == tags.end() || !found->is_string())
        return std::nullopt;
    auto value = found->get<std::string>();
    if (value.empty())
        return std::nullopt;
    return value;
}

bool sameCoordinate(GeoCoord first, GeoCoord second) {
    return std::abs(first.latitude_deg - second.latitude_deg) <= 1e-12 &&
           std::abs(first.longitude_deg - second.longitude_deg) <= 1e-12;
}

bool mergeString(std::optional<std::string>& target, const std::optional<std::string>& source) {
    if (!target) {
        target = source;
        return true;
    }
    return !source || *target == *source;
}

std::optional<double> parseElevation(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n'))
        text.remove_suffix(1);
    if (text.empty())
        return std::nullopt;
    if (text.front() == '+')
        text.remove_prefix(1);
    if (text.empty())
        return std::nullopt;
    double value = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(value) || value < -500.0 || value > 9000.0)
        return std::nullopt;
    return value;
}

Result<std::vector<PeakId>> readSelection(const std::filesystem::path& file) {
    auto loaded = readJson(file, "peak selection");
    if (const auto* error = std::get_if<Error>(&loaded))
        return *error;
    const auto& document = std::get<Json>(loaded);
    if (!document.is_object() || !document.contains("schema_version") ||
        !document["schema_version"].is_number_integer() ||
        document["schema_version"].get<int>() != 1 || !document.contains("osm_ids") ||
        !document["osm_ids"].is_array())
        return invalid("Peak selection must contain schema_version=1 and an osm_ids array");
    for (const auto& item : document.items()) {
        if (item.key() != "schema_version" && item.key() != "description" &&
            item.key() != "osm_ids")
            return Error{ErrorCode::UnsupportedFormat,
                         "Unsupported peak selection field: " + item.key()};
    }
    if (document.contains("description") && !document["description"].is_string())
        return invalid("Peak selection description must be a string");
    std::vector<PeakId> ids;
    for (const auto& value : document["osm_ids"]) {
        if (!value.is_string() || value.get_ref<const std::string&>().empty())
            return invalid("Every selected OSM id must be a non-empty string");
        ids.push_back(value.get<std::string>());
    }
    return ids;
}

Result<std::vector<PeakCorrection>> readCorrections(const std::filesystem::path& file) {
    auto loaded = readJson(file, "peak corrections");
    if (const auto* error = std::get_if<Error>(&loaded))
        return *error;
    const auto& document = std::get<Json>(loaded);
    if (!document.is_object() || !document.contains("schema_version") ||
        !document["schema_version"].is_number_integer() ||
        document["schema_version"].get<int>() != 1 || !document.contains("corrections") ||
        !document["corrections"].is_array())
        return invalid("Peak corrections must contain schema_version=1 and a corrections array");
    for (const auto& item : document.items()) {
        if (item.key() != "schema_version" && item.key() != "description" &&
            item.key() != "corrections")
            return Error{ErrorCode::UnsupportedFormat,
                         "Unsupported peak corrections field: " + item.key()};
    }
    std::vector<PeakCorrection> corrections;
    for (const auto& value : document["corrections"]) {
        if (!value.is_object() || !value.contains("id") || !value["id"].is_string() ||
            value["id"].get_ref<const std::string&>().empty())
            return invalid("Every peak correction must have a non-empty string id");
        for (const auto& item : value.items()) {
            if (item.key() != "id" && item.key() != "name" && item.key() != "display_elevation" &&
                item.key() != "note")
                return Error{ErrorCode::UnsupportedFormat,
                             "Unsupported peak correction field: " + item.key()};
        }
        PeakCorrection correction;
        correction.id = value["id"].get<std::string>();
        if (value.contains("name")) {
            if (!value["name"].is_string() || value["name"].get_ref<const std::string&>().empty())
                return invalid("Corrected peak name must be a non-empty string");
            correction.name = value["name"].get<std::string>();
        }
        if (value.contains("display_elevation")) {
            const auto& elevation = value["display_elevation"];
            if (elevation.is_null()) {
                correction.clear_display_elevation = true;
            } else {
                if (!elevation.is_object() || !elevation.contains("metres") ||
                    !elevation["metres"].is_number() || !elevation.contains("vertical_reference") ||
                    !elevation["vertical_reference"].is_string() || !elevation.contains("source") ||
                    !elevation["source"].is_string())
                    return invalid("Corrected display_elevation is malformed");
                const double metres = elevation["metres"].get<double>();
                const auto vertical = elevation["vertical_reference"].get<std::string>();
                const auto source = elevation["source"].get<std::string>();
                if (!std::isfinite(metres) || vertical.empty() || source.empty())
                    return invalid(
                        "Corrected display_elevation values must be finite and non-empty");
                correction.display_elevation = CatalogElevation{metres, vertical, source};
            }
        }
        if (!value.contains("note") || !value["note"].is_string() ||
            value["note"].get_ref<const std::string&>().empty())
            return invalid("Every peak correction must contain a non-empty note");
        correction.note = value["note"].get<std::string>();
        if (!correction.name && !correction.display_elevation &&
            !correction.clear_display_elevation)
            return invalid("Peak correction does not change any value");
        corrections.push_back(std::move(correction));
    }
    return corrections;
}

Json optionalStringJson(const std::optional<std::string>& value) {
    return value ? Json(*value) : Json(nullptr);
}

Status writePeakCatalogJson(const PreparedPeakCatalog& catalog, const PeakProvenance& provenance,
                            const std::filesystem::path& output) {
    try {
        std::map<std::string, std::size_t> reasons;
        Json issues = Json::array();
        for (const auto& issue : catalog.report.issues) {
            ++reasons[issue.reason];
            issues.push_back({{"id", issue.id.empty() ? Json(nullptr) : Json(issue.id)},
                              {"reason", issue.reason},
                              {"detail", issue.detail}});
        }
        Json reason_counts = Json::object();
        for (const auto& [reason, count] : reasons)
            reason_counts[reason] = count;
        const auto& report = catalog.report;
        Json document = {{"schema_version", 1},
                         {"tool_version", tatra::version},
                         {"source",
                          {{"osm_file", provenance.raw_osm.filename().string()},
                           {"osm_sha256", provenance.raw_osm_sha256},
                           {"terrain_file", provenance.terrain.filename().string()},
                           {"terrain_sha256", provenance.terrain_sha256},
                           {"selection_file", provenance.selection.filename().string()},
                           {"selection_sha256", provenance.selection_sha256},
                           {"corrections_file", provenance.corrections.filename().string()},
                           {"corrections_sha256", provenance.corrections_sha256}}},
                         {"report",
                          {{"input_element_count", report.input_element_count},
                           {"parsed_peak_count", report.parsed_peak_count},
                           {"selected_count", report.selected_count},
                           {"unselected_count", report.unselected_count},
                           {"accepted_count", report.accepted_count},
                           {"rejected_count", report.rejected_count},
                           {"duplicate_count", report.duplicate_count},
                           {"missing_ele_count", report.missing_ele_count},
                           {"invalid_ele_count", report.invalid_ele_count},
                           {"reason_counts", std::move(reason_counts)},
                           {"issues", std::move(issues)}}},
                         {"peaks", Json::array()}};
        for (const auto& record : catalog.peaks) {
            const auto& peak = record.peak;
            Json display_elevation = nullptr;
            if (peak.display_elevation) {
                display_elevation = {
                    {"metres", peak.display_elevation->metres},
                    {"vertical_reference", peak.display_elevation->vertical_reference},
                    {"source", peak.display_elevation->source}};
            }
            Json correction =
                record.correction_note ? Json(*record.correction_note) : Json(nullptr);
            document["peaks"].push_back(
                {{"id", peak.id},
                 {"name", peak.name},
                 {"geometry_position",
                  {{"latitude_deg", peak.geometry_position.coord.latitude_deg},
                   {"longitude_deg", peak.geometry_position.coord.longitude_deg},
                   {"ellipsoid_height_m", peak.geometry_position.ellipsoid_height_m}}},
                 {"geometry_height_source", "DemSample"},
                 {"display_elevation", std::move(display_elevation)},
                 {"source",
                  {{"osm_type", "node"},
                   {"osm_id", record.osm_id},
                   {"names",
                    {{"name", optionalStringJson(record.source_names.name)},
                     {"name:pl", optionalStringJson(record.source_names.name_pl)},
                     {"name:sk", optionalStringJson(record.source_names.name_sk)}}},
                   {"ele", optionalStringJson(record.source_ele)}}},
                 {"correction_note", std::move(correction)}});
        }
        const auto serialized = document.dump(2) + '\n';
        std::ofstream stream;
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(output, std::ios::binary | std::ios::trunc);
        stream << serialized;
        stream.close();
        return std::monostate{};
    } catch (const Json::exception& error) {
        return invalid("Cannot serialize peak catalog JSON: " + std::string{error.what()});
    } catch (const std::ios_base::failure& error) {
        return Error{ErrorCode::IoFailure,
                     "Cannot write peak catalog: " + std::string{error.what()}};
    }
}

} // namespace

Result<OsmPeakImport> parseOsmPeakNodes(const std::filesystem::path& file) {
    auto loaded = readJson(file, "OSM export");
    if (const auto* error = std::get_if<Error>(&loaded))
        return *error;
    const auto& document = std::get<Json>(loaded);
    if (!document.is_object() || !document.contains("elements") || !document["elements"].is_array())
        return invalid("OSM export must contain an elements array");

    OsmPeakImport output;
    for (const auto& element : document["elements"]) {
        ++output.report.input_element_count;
        if (!element.is_object()) {
            ++output.report.rejected_count;
            addIssue(output.report, {}, "malformed_element", "OSM element is not an object");
            continue;
        }
        const auto type = element.find("type");
        const auto tags = element.find("tags");
        if (type == element.end() || !type->is_string() || type->get<std::string>() != "node" ||
            tags == element.end() || !tags->is_object() || !tags->contains("natural") ||
            !(*tags)["natural"].is_string() || (*tags)["natural"].get<std::string>() != "peak") {
            ++output.report.rejected_count;
            addIssue(output.report, {}, "not_peak_node", "OSM element is not a natural=peak node");
            continue;
        }
        const auto id = element.find("id");
        const auto latitude = element.find("lat");
        const auto longitude = element.find("lon");
        const bool valid_id = id != element.end() &&
                              (id->is_number_unsigned() || id->is_number_integer()) &&
                              (!id->is_number_integer() || id->get<std::int64_t>() > 0);
        const bool valid_coord = latitude != element.end() && latitude->is_number() &&
                                 longitude != element.end() && longitude->is_number();
        if (!valid_id || !valid_coord) {
            ++output.report.rejected_count;
            addIssue(output.report, {}, "malformed_peak", "Peak id or coordinates are malformed");
            continue;
        }
        const auto osm_id = id->get<std::uint64_t>();
        const double latitude_deg = latitude->get<double>();
        const double longitude_deg = longitude->get<double>();
        const auto stable_id = "osm:node:" + std::to_string(osm_id);
        if (!std::isfinite(latitude_deg) || !std::isfinite(longitude_deg) || latitude_deg < -90.0 ||
            latitude_deg > 90.0 || longitude_deg < -180.0 || longitude_deg > 180.0) {
            ++output.report.rejected_count;
            addIssue(output.report, stable_id, "invalid_coordinate",
                     "Peak coordinates are outside WGS84 bounds");
            continue;
        }
        OsmPeakNode peak;
        peak.id = stable_id;
        peak.osm_id = osm_id;
        peak.coord = {latitude_deg, longitude_deg};
        peak.source_names.name = stringTag(*tags, "name");
        peak.source_names.name_pl = stringTag(*tags, "name:pl");
        peak.source_names.name_sk = stringTag(*tags, "name:sk");
        const auto elevation = tags->find("ele");
        if (elevation != tags->end()) {
            if (elevation->is_string())
                peak.source_ele = elevation->get<std::string>();
            else
                peak.source_ele_has_invalid_type = true;
        }
        output.peaks.push_back(std::move(peak));
        ++output.report.parsed_peak_count;
    }
    return output;
}

Result<OsmPeakImport> validateAndDeduplicatePeaks(OsmPeakImport input,
                                                  const std::vector<PeakId>& selected_ids,
                                                  const std::vector<PeakCorrection>& corrections) {
    if (selected_ids.empty())
        return invalid("Peak selection must not be empty");
    std::unordered_set<PeakId> selected_set;
    for (const auto& id : selected_ids) {
        if (id.empty() || !selected_set.insert(id).second)
            return invalid("Peak selection contains an empty or duplicate id: " + id);
    }
    std::unordered_map<PeakId, PeakCorrection> correction_by_id;
    for (const auto& correction : corrections) {
        if (!selected_set.contains(correction.id))
            return invalid("Correction refers to an unselected peak: " + correction.id);
        if (correction.note.empty())
            return invalid("Correction must explain its source: " + correction.id);
        if (correction.name && correction.name->empty())
            return invalid("Corrected peak name must not be empty: " + correction.id);
        if (correction.clear_display_elevation && correction.display_elevation)
            return invalid("Correction cannot clear and replace elevation: " + correction.id);
        if (correction.display_elevation &&
            (!std::isfinite(correction.display_elevation->metres) ||
             correction.display_elevation->vertical_reference.empty() ||
             correction.display_elevation->source.empty()))
            return invalid("Corrected elevation is invalid: " + correction.id);
        if (!correction_by_id.emplace(correction.id, correction).second)
            return invalid("Duplicate correction for peak: " + correction.id);
    }

    std::vector<OsmPeakNode> unique;
    std::unordered_map<PeakId, std::size_t> index_by_id;
    for (auto& peak : input.peaks) {
        const auto [found, inserted] = index_by_id.emplace(peak.id, unique.size());
        if (inserted) {
            unique.push_back(std::move(peak));
            continue;
        }
        ++input.report.duplicate_count;
        auto& existing = unique[found->second];
        if (existing.osm_id != peak.osm_id || !sameCoordinate(existing.coord, peak.coord) ||
            !mergeString(existing.source_names.name, peak.source_names.name) ||
            !mergeString(existing.source_names.name_pl, peak.source_names.name_pl) ||
            !mergeString(existing.source_names.name_sk, peak.source_names.name_sk) ||
            !mergeString(existing.source_ele, peak.source_ele))
            return Error{ErrorCode::CorruptData, "Conflicting duplicate OSM id: " + peak.id};
        existing.source_ele_has_invalid_type =
            existing.source_ele_has_invalid_type || peak.source_ele_has_invalid_type;
    }

    OsmPeakImport output;
    output.report = std::move(input.report);
    output.report.selected_count = selected_ids.size();
    output.report.unselected_count =
        unique.size() > selected_ids.size() ? unique.size() - selected_ids.size() : 0;
    output.peaks.reserve(selected_ids.size());
    for (const auto& id : selected_ids) {
        const auto found = index_by_id.find(id);
        if (found == index_by_id.end())
            return invalid("Selected peak is absent from OSM export: " + id);
        auto peak = std::move(unique[found->second]);
        const auto* preferred = peak.source_names.name_pl ? &peak.source_names.name_pl
                                : peak.source_names.name  ? &peak.source_names.name
                                                          : &peak.source_names.name_sk;
        if (!*preferred) {
            ++output.report.rejected_count;
            addIssue(output.report, peak.id, "missing_name",
                     "Selected peak has no name:pl, name or name:sk");
            continue;
        }
        peak.selected_name = **preferred;
        if (peak.source_ele_has_invalid_type) {
            ++output.report.invalid_ele_count;
            addIssue(output.report, peak.id, "invalid_ele",
                     "OSM ele is not a string and was ignored");
        } else if (!peak.source_ele) {
            ++output.report.missing_ele_count;
            addIssue(output.report, peak.id, "missing_ele", "OSM peak has no ele tag");
        } else if (const auto metres = parseElevation(*peak.source_ele)) {
            peak.display_elevation = CatalogElevation{*metres, "unspecified", "OpenStreetMap ele"};
        } else {
            ++output.report.invalid_ele_count;
            addIssue(output.report, peak.id, "invalid_ele",
                     "Ambiguous or out-of-range OSM ele was ignored: " + *peak.source_ele);
        }
        if (const auto correction = correction_by_id.find(peak.id);
            correction != correction_by_id.end()) {
            if (correction->second.name)
                peak.selected_name = *correction->second.name;
            if (correction->second.clear_display_elevation)
                peak.display_elevation.reset();
            if (correction->second.display_elevation)
                peak.display_elevation = correction->second.display_elevation;
            peak.correction_note = correction->second.note;
        }
        output.peaks.push_back(std::move(peak));
    }
    return output;
}

Result<PreparedPeakCatalog> assignGeometryHeights(OsmPeakImport input, const ITerrain& terrain) {
    PreparedPeakCatalog output;
    output.report = std::move(input.report);
    output.peaks.reserve(input.peaks.size());
    for (auto& source : input.peaks) {
        const auto height = terrain.sampleHeight(source.coord);
        const auto* sample = std::get_if<HeightSample>(&height);
        if (!sample || !std::isfinite(sample->ellipsoid_height_m)) {
            ++output.report.rejected_count;
            const bool outside =
                std::holds_alternative<TerrainSampleError>(height) &&
                std::get<TerrainSampleError>(height) == TerrainSampleError::OutsideCoverage;
            addIssue(output.report, source.id, outside ? "outside_dem" : "dem_nodata",
                     outside ? "Peak lies outside DEM coverage"
                             : "DEM has no valid height at the peak");
            continue;
        }
        Peak peak;
        peak.id = source.id;
        peak.name = source.selected_name;
        peak.geometry_position = {source.coord, sample->ellipsoid_height_m};
        peak.geometry_height_source = GeometryHeightSource::DemSample;
        peak.display_elevation = source.display_elevation;
        output.peaks.push_back({std::move(peak), source.osm_id, std::move(source.source_names),
                                std::move(source.source_ele), std::move(source.correction_note)});
        ++output.report.accepted_count;
    }
    return output;
}

Result<PeakImportReport> preparePeakCatalog(const PeakPrepareOptions& options) {
    if (options.raw_osm.empty() || options.terrain.empty() || options.selection.empty() ||
        options.corrections.empty() || options.output_directory.empty())
        return invalid("OSM, terrain, selection, corrections and output paths are required");
    auto selection = readSelection(options.selection);
    if (const auto* error = std::get_if<Error>(&selection))
        return *error;
    const auto& selected_ids = std::get<std::vector<PeakId>>(selection);
    if (selected_ids.size() < 30 || selected_ids.size() > 100)
        return invalid("Production peak selection must contain between 30 and 100 ids");
    auto corrections = readCorrections(options.corrections);
    if (const auto* error = std::get_if<Error>(&corrections))
        return *error;
    auto parsed = parseOsmPeakNodes(options.raw_osm);
    if (const auto* error = std::get_if<Error>(&parsed))
        return *error;
    auto validated =
        validateAndDeduplicatePeaks(std::get<OsmPeakImport>(std::move(parsed)), selected_ids,
                                    std::get<std::vector<PeakCorrection>>(std::move(corrections)));
    if (const auto* error = std::get_if<Error>(&validated))
        return *error;
    auto terrain = loadNormalizedGeoTiff(options.terrain);
    if (const auto* error = std::get_if<Error>(&terrain))
        return *error;
    auto prepared = assignGeometryHeights(std::get<OsmPeakImport>(std::move(validated)),
                                          std::get<GridTerrain>(terrain));
    if (const auto* error = std::get_if<Error>(&prepared))
        return *error;

    PeakProvenance provenance;
    provenance.raw_osm = options.raw_osm;
    provenance.terrain = options.terrain;
    provenance.selection = options.selection;
    provenance.corrections = options.corrections;
    auto osm_hash = detail::sha256File(options.raw_osm);
    if (const auto* error = std::get_if<Error>(&osm_hash))
        return *error;
    provenance.raw_osm_sha256 = std::get<std::string>(std::move(osm_hash));
    auto terrain_hash = detail::sha256File(options.terrain);
    if (const auto* error = std::get_if<Error>(&terrain_hash))
        return *error;
    provenance.terrain_sha256 = std::get<std::string>(std::move(terrain_hash));
    auto selection_hash = detail::sha256File(options.selection);
    if (const auto* error = std::get_if<Error>(&selection_hash))
        return *error;
    provenance.selection_sha256 = std::get<std::string>(std::move(selection_hash));
    auto corrections_hash = detail::sha256File(options.corrections);
    if (const auto* error = std::get_if<Error>(&corrections_hash))
        return *error;
    provenance.corrections_sha256 = std::get<std::string>(std::move(corrections_hash));

    std::error_code directory_error;
    std::filesystem::create_directories(options.output_directory, directory_error);
    if (directory_error)
        return Error{ErrorCode::IoFailure,
                     "Cannot create output directory: " + directory_error.message()};
    auto catalog = std::get<PreparedPeakCatalog>(std::move(prepared));
    const auto written =
        writePeakCatalogJson(catalog, provenance, options.output_directory / "peaks.json");
    if (const auto* error = std::get_if<Error>(&written))
        return *error;
    return catalog.report;
}
} // namespace tatra::desktop
