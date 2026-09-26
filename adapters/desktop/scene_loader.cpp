#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <string_view>
#include <tatra/desktop/io.hpp>
#include <tatra/validation.hpp>
#include <utility>
#include <vector>

namespace tatra::desktop {
namespace {
using Json = nlohmann::json;

void object(const Json& value, std::initializer_list<std::string_view> keys,
            const std::string& path) {
    if (!value.is_object())
        throw Error{ErrorCode::InvalidInput, path + " must be an object"};
    for (const auto& item : value.items()) {
        bool known = false;
        for (const auto key : keys)
            known = known || item.key() == key;
        if (!known)
            throw Error{ErrorCode::UnsupportedFormat, path + ": unsupported field " + item.key()};
    }
}

double number(const Json& value, const std::string& path) {
    if (!value.is_number())
        throw Error{ErrorCode::InvalidInput, path + " must be a number"};
    const double result = value.get<double>();
    if (!std::isfinite(result))
        throw Error{ErrorCode::InvalidInput, path + " must be finite"};
    return result;
}

std::uint64_t integer(const Json& value, std::uint64_t maximum, const std::string& path) {
    if (!value.is_number_integer() ||
        (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)) {
        throw Error{ErrorCode::InvalidInput, path + " must be a non-negative integer"};
    }
    const auto result = value.get<std::uint64_t>();
    if (result > maximum)
        throw Error{ErrorCode::InvalidInput, path + " is out of range"};
    return result;
}

template <class T> T checked(Result<T> result) {
    if (const auto* error = std::get_if<Error>(&result))
        throw *error;
    return std::get<T>(std::move(result));
}
} // namespace

Result<FrameQuery> loadScene(const std::filesystem::path& file, const ITerrain& terrain) {
    try {
        std::ifstream input(file, std::ios::binary);
        if (!input)
            return Error{ErrorCode::IoFailure, "Cannot open scene: " + file.string()};
        input.exceptions(std::ios::badbit);
        const std::string contents{std::istreambuf_iterator<char>{input},
                                   std::istreambuf_iterator<char>{}};
        // Reject duplicate keys instead of silently accepting the last value.
        std::vector<std::vector<std::string>> object_keys;
        const auto document =
            Json::parse(contents, [&](int, Json::parse_event_t event, Json& parsed) {
                if (event == Json::parse_event_t::object_start)
                    object_keys.emplace_back();
                if (event == Json::parse_event_t::key) {
                    const auto key = parsed.get<std::string>();
                    for (const auto& previous : object_keys.back()) {
                        if (previous == key)
                            throw Error{ErrorCode::InvalidInput, "Duplicate JSON key: " + key};
                    }
                    object_keys.back().push_back(key);
                }
                if (event == Json::parse_event_t::object_end)
                    object_keys.pop_back();
                return true;
            });
        object(document,
               {"schema_version", "timestamp_ns", "observer", "camera", "orientation", "options"},
               "scene");
        if (integer(document.at("schema_version"), std::numeric_limits<std::uint64_t>::max(),
                    "schema_version") != 1)
            return Error{ErrorCode::UnsupportedFormat, "Only scene schema_version=1 is supported"};

        FrameQuery query;
        query.timestamp = Timestamp{static_cast<Timestamp::rep>(
            integer(document.at("timestamp_ns"),
                    static_cast<std::uint64_t>(std::numeric_limits<Timestamp::rep>::max()),
                    "timestamp_ns"))};
        const auto& observer = document.at("observer");
        object(observer, {"latitude_deg", "longitude_deg", "height", "simulated"}, "observer");
        const auto coord =
            checked(makeGeoCoord(number(observer.at("latitude_deg"), "latitude_deg"),
                                 number(observer.at("longitude_deg"), "longitude_deg")));
        const auto& height = observer.at("height");
        object(height, {"mode", "metres"}, "observer.height");
        if (!height.at("mode").is_string())
            throw Error{ErrorCode::InvalidInput, "height.mode must be a string"};
        const auto mode = height.at("mode").get<std::string>();
        double metres = number(height.at("metres"), "height.metres");
        if (mode == "above_dem") {
            const auto sampled = terrain.sampleHeight(coord);
            const auto* sample = std::get_if<HeightSample>(&sampled);
            if (!sample || !std::isfinite(sample->ellipsoid_height_m))
                return Error{ErrorCode::InvalidInput,
                             "above_dem requires a finite terrain height at the observer"};
            metres += sample->ellipsoid_height_m;
        } else if (mode != "ellipsoid") {
            return Error{ErrorCode::UnsupportedFormat, "Unsupported height.mode: " + mode};
        }
        query.observer.position = checked(makeGeoPosition(coord, metres));
        if (observer.contains("simulated")) {
            if (!observer.at("simulated").is_boolean())
                return Error{ErrorCode::InvalidInput, "observer.simulated must be a boolean"};
            query.observer.simulated = observer.at("simulated").get<bool>();
        }

        const auto& camera = document.at("camera");
        object(camera,
               {"width_px", "height_px", "horizontal_fov_deg", "fx_px", "fy_px", "cx_px", "cy_px"},
               "camera");
        const int width = static_cast<int>(
            integer(camera.at("width_px"), std::numeric_limits<int>::max(), "width_px"));
        const int height_px = static_cast<int>(
            integer(camera.at("height_px"), std::numeric_limits<int>::max(), "height_px"));
        const bool hfov = camera.contains("horizontal_fov_deg");
        const bool intrinsics = camera.contains("fx_px") || camera.contains("fy_px") ||
                                camera.contains("cx_px") || camera.contains("cy_px");
        if (hfov && intrinsics)
            return Error{ErrorCode::InvalidInput,
                         "Specify either HFOV or full intrinsics, not both"};
        if (hfov) {
            query.camera = checked(cameraFromHorizontalFov(
                width, height_px, number(camera.at("horizontal_fov_deg"), "horizontal_fov_deg")));
        } else {
            query.camera = checked(validateCamera(
                {width, height_px, number(camera.at("fx_px"), "fx_px"),
                 number(camera.at("fy_px"), "fy_px"), number(camera.at("cx_px"), "cx_px"),
                 number(camera.at("cy_px"), "cy_px")}));
        }
        const auto& orientation = document.at("orientation");
        object(orientation, {"heading_deg", "pitch_deg", "roll_deg"}, "orientation");
        query.pose =
            checked(poseFromHeadingPitchRoll(number(orientation.at("heading_deg"), "heading_deg"),
                                             number(orientation.at("pitch_deg"), "pitch_deg"),
                                             number(orientation.at("roll_deg"), "roll_deg")));
        if (document.contains("options")) {
            const auto& options = document.at("options");
            object(options, {"min_range_m", "max_range_m", "near_z_m", "visibility"}, "options");
            if (options.contains("min_range_m"))
                query.options.min_range_m = number(options.at("min_range_m"), "min_range_m");
            if (options.contains("max_range_m"))
                query.options.max_range_m = number(options.at("max_range_m"), "max_range_m");
            if (options.contains("near_z_m"))
                query.options.near_z_m = number(options.at("near_z_m"), "near_z_m");
            if (options.contains("visibility")) {
                const auto& visibility = options.at("visibility");
                object(visibility,
                       {"sample_step_m", "clearance_band_m", "start_guard_m", "target_guard_m",
                        "max_samples"},
                       "visibility");
                auto& target = query.options.visibility;
                if (visibility.contains("sample_step_m"))
                    target.sample_step_m = number(visibility.at("sample_step_m"), "sample_step_m");
                if (visibility.contains("clearance_band_m"))
                    target.clearance_band_m =
                        number(visibility.at("clearance_band_m"), "clearance_band_m");
                if (visibility.contains("start_guard_m"))
                    target.start_guard_m = number(visibility.at("start_guard_m"), "start_guard_m");
                if (visibility.contains("target_guard_m"))
                    target.target_guard_m =
                        number(visibility.at("target_guard_m"), "target_guard_m");
                if (visibility.contains("max_samples"))
                    target.max_samples = static_cast<std::size_t>(
                        integer(visibility.at("max_samples"),
                                std::numeric_limits<std::size_t>::max(), "max_samples"));
            }
        }
        query.options = checked(validateOptions(query.options));
        return query;
    } catch (const Error& error) {
        return error;
    } catch (const Json::exception& error) {
        return Error{ErrorCode::InvalidInput, "Invalid scene JSON: " + std::string{error.what()}};
    } catch (const std::ios_base::failure& error) {
        return Error{ErrorCode::IoFailure, "Cannot read scene: " + std::string{error.what()}};
    }
}
} // namespace tatra::desktop
