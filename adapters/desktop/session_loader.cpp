#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <tatra/camera.hpp>
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
        const bool known = std::any_of(keys.begin(), keys.end(),
                                       [&](std::string_view key) { return item.key() == key; });
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
        (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        throw Error{ErrorCode::InvalidInput, path + " must be a non-negative integer"};
    const auto result = value.get<std::uint64_t>();
    if (result > maximum)
        throw Error{ErrorCode::InvalidInput, path + " is out of range"};
    return result;
}

std::string text(const Json& value, const std::string& path) {
    if (!value.is_string())
        throw Error{ErrorCode::InvalidInput, path + " must be a string"};
    return value.get<std::string>();
}

template <class T> T checked(Result<T> result) {
    if (const auto* error = std::get_if<Error>(&result))
        throw *error;
    return std::get<T>(std::move(result));
}

Json readJson(const std::filesystem::path& file, const std::string& description) {
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw Error{ErrorCode::IoFailure, "Cannot open " + description + ": " + file.string()};
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
                            description + " contains duplicate JSON key: " + key};
            object_keys.back().push_back(key);
        }
        if (event == Json::parse_event_t::object_end)
            object_keys.pop_back();
        return true;
    });
}

std::filesystem::path relativeFile(const Json& value, const std::string& path) {
    const std::filesystem::path result{text(value, path)};
    if (result.empty() || result.is_absolute())
        throw Error{ErrorCode::InvalidInput, path + " must be a non-empty relative path"};
    for (const auto& component : result) {
        if (component == "..")
            throw Error{ErrorCode::InvalidInput, path + " must not leave the session directory"};
    }
    return result.lexically_normal();
}

Observer parseObserver(const Json& value, const ITerrain& terrain) {
    object(value, {"latitude_deg", "longitude_deg", "height", "simulated"}, "observer");
    const auto coord = checked(makeGeoCoord(number(value.at("latitude_deg"), "latitude_deg"),
                                            number(value.at("longitude_deg"), "longitude_deg")));
    const auto& height = value.at("height");
    object(height, {"mode", "metres"}, "observer.height");
    const auto mode = text(height.at("mode"), "observer.height.mode");
    double metres = number(height.at("metres"), "observer.height.metres");
    if (mode == "above_dem") {
        const auto sampled = terrain.sampleHeight(coord);
        const auto* sample = std::get_if<HeightSample>(&sampled);
        if (!sample || !std::isfinite(sample->ellipsoid_height_m))
            throw Error{ErrorCode::InvalidInput,
                        "above_dem requires a finite terrain height at the observer"};
        metres += sample->ellipsoid_height_m;
    } else if (mode != "ellipsoid") {
        throw Error{ErrorCode::UnsupportedFormat, "Unsupported height.mode: " + mode};
    }
    Observer observer;
    observer.position = checked(makeGeoPosition(coord, metres));
    if (value.contains("simulated")) {
        if (!value.at("simulated").is_boolean())
            throw Error{ErrorCode::InvalidInput, "observer.simulated must be a boolean"};
        observer.simulated = value.at("simulated").get<bool>();
    }
    return observer;
}

CameraIntrinsics parseCamera(const Json& value) {
    object(value,
           {"width_px", "height_px", "horizontal_fov_deg", "fx_px", "fy_px", "cx_px", "cy_px"},
           "camera");
    const int width = static_cast<int>(
        integer(value.at("width_px"), std::numeric_limits<int>::max(), "camera.width_px"));
    const int height = static_cast<int>(
        integer(value.at("height_px"), std::numeric_limits<int>::max(), "camera.height_px"));
    const bool hfov = value.contains("horizontal_fov_deg");
    const bool intrinsics = value.contains("fx_px") || value.contains("fy_px") ||
                            value.contains("cx_px") || value.contains("cy_px");
    if (hfov && intrinsics)
        throw Error{ErrorCode::InvalidInput, "Specify either HFOV or full intrinsics, not both"};
    if (hfov)
        return checked(cameraFromHorizontalFov(
            width, height, number(value.at("horizontal_fov_deg"), "camera.horizontal_fov_deg")));
    return checked(validateCamera({width, height, number(value.at("fx_px"), "camera.fx_px"),
                                   number(value.at("fy_px"), "camera.fy_px"),
                                   number(value.at("cx_px"), "camera.cx_px"),
                                   number(value.at("cy_px"), "camera.cy_px")}));
}

QueryOptions parseOptions(const Json& value) {
    object(value, {"min_range_m", "max_range_m", "near_z_m", "visibility"}, "options");
    QueryOptions options;
    if (value.contains("min_range_m"))
        options.min_range_m = number(value.at("min_range_m"), "options.min_range_m");
    if (value.contains("max_range_m"))
        options.max_range_m = number(value.at("max_range_m"), "options.max_range_m");
    if (value.contains("near_z_m"))
        options.near_z_m = number(value.at("near_z_m"), "options.near_z_m");
    if (value.contains("visibility")) {
        const auto& visibility = value.at("visibility");
        object(
            visibility,
            {"sample_step_m", "clearance_band_m", "start_guard_m", "target_guard_m", "max_samples"},
            "options.visibility");
        auto& target = options.visibility;
        if (visibility.contains("sample_step_m"))
            target.sample_step_m =
                number(visibility.at("sample_step_m"), "options.visibility.sample_step_m");
        if (visibility.contains("clearance_band_m"))
            target.clearance_band_m =
                number(visibility.at("clearance_band_m"), "options.visibility.clearance_band_m");
        if (visibility.contains("start_guard_m"))
            target.start_guard_m =
                number(visibility.at("start_guard_m"), "options.visibility.start_guard_m");
        if (visibility.contains("target_guard_m"))
            target.target_guard_m =
                number(visibility.at("target_guard_m"), "options.visibility.target_guard_m");
        if (visibility.contains("max_samples"))
            target.max_samples = static_cast<std::size_t>(
                integer(visibility.at("max_samples"), std::numeric_limits<std::size_t>::max(),
                        "options.visibility.max_samples"));
    }
    return checked(validateOptions(options));
}

Timestamp timestamp(const Json& value, const std::string& path) {
    return Timestamp{static_cast<Timestamp::rep>(integer(
        value, static_cast<std::uint64_t>(std::numeric_limits<Timestamp::rep>::max()), path))};
}

std::vector<PoseSample> parsePoses(const Json& value) {
    if (!value.is_array())
        throw Error{ErrorCode::InvalidInput, "poses must be an array"};
    std::vector<PoseSample> poses;
    poses.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto path = "poses[" + std::to_string(index) + "]";
        const auto& item = value[index];
        object(item, {"timestamp_ns", "camera_from_enu"}, path);
        const auto& quaternion = item.at("camera_from_enu");
        object(quaternion, {"w", "x", "y", "z"}, path + ".camera_from_enu");
        CameraPose pose{
            Eigen::Quaterniond{number(quaternion.at("w"), path + ".camera_from_enu.w"),
                               number(quaternion.at("x"), path + ".camera_from_enu.x"),
                               number(quaternion.at("y"), path + ".camera_from_enu.y"),
                               number(quaternion.at("z"), path + ".camera_from_enu.z")}};
        pose = checked(validatePose(pose));
        poses.push_back({timestamp(item.at("timestamp_ns"), path + ".timestamp_ns"), pose});
    }
    const auto validated = PoseTimeline::create(poses);
    if (const auto* error = std::get_if<Error>(&validated))
        throw *error;
    return poses;
}

std::vector<ReplayFrame> parseFrames(const Json& value) {
    if (!value.is_array() || value.empty())
        throw Error{ErrorCode::InvalidInput, "frames must be a non-empty array"};
    std::vector<ReplayFrame> frames;
    frames.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto path = "frames[" + std::to_string(index) + "]";
        const auto& item = value[index];
        object(item, {"timestamp_ns", "image_path"}, path);
        ReplayFrame frame;
        frame.timestamp = timestamp(item.at("timestamp_ns"), path + ".timestamp_ns");
        if (item.contains("image_path"))
            frame.image_path =
                relativeFile(item.at("image_path"), path + ".image_path").generic_string();
        frames.push_back(std::move(frame));
    }
    return frames;
}

const char* errorCodeName(ErrorCode code) {
    switch (code) {
    case ErrorCode::InvalidInput:
        return "InvalidInput";
    case ErrorCode::UnsupportedFormat:
        return "UnsupportedFormat";
    case ErrorCode::IoFailure:
        return "IoFailure";
    case ErrorCode::CorruptData:
        return "CorruptData";
    case ErrorCode::TimeOutsideSession:
        return "TimeOutsideSession";
    case ErrorCode::TimeGapTooLarge:
        return "TimeGapTooLarge";
    case ErrorCode::NotImplemented:
        return "NotImplemented";
    }
    throw Error{ErrorCode::InvalidInput, "Unknown error code"};
}

Json optionsJson(const QueryOptions& options) {
    return {{"min_range_m", options.min_range_m},
            {"max_range_m", options.max_range_m},
            {"near_z_m", options.near_z_m},
            {"visibility",
             {{"sample_step_m", options.visibility.sample_step_m},
              {"clearance_band_m", options.visibility.clearance_band_m},
              {"start_guard_m", options.visibility.start_guard_m},
              {"target_guard_m", options.visibility.target_guard_m},
              {"max_samples", options.visibility.max_samples}}}};
}

void writeJson(const Json& document, const std::filesystem::path& output) {
    const auto serialized = document.dump(2) + '\n';
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(output, std::ios::binary | std::ios::trunc);
    stream << serialized;
    stream.close();
}
} // namespace

Result<Session> loadSession(const std::filesystem::path& directory, const ITerrain& terrain) {
    try {
        const auto document = readJson(directory / "session.json", "session");
        object(document,
               {"schema_version", "observer", "camera", "options", "max_gap_ns", "poses_file",
                "frames_file"},
               "session");
        if (integer(document.at("schema_version"), std::numeric_limits<std::uint64_t>::max(),
                    "schema_version") != 1)
            return Error{ErrorCode::UnsupportedFormat,
                         "Only session schema_version=1 is supported"};

        Session session;
        session.observer = parseObserver(document.at("observer"), terrain);
        session.camera = parseCamera(document.at("camera"));
        if (document.contains("options"))
            session.options = parseOptions(document.at("options"));
        else
            session.options = checked(validateOptions(session.options));
        session.max_gap = timestamp(document.at("max_gap_ns"), "max_gap_ns");
        const auto poses_file = relativeFile(document.at("poses_file"), "poses_file");
        const auto frames_file = relativeFile(document.at("frames_file"), "frames_file");
        session.poses = parsePoses(readJson(directory / poses_file, "pose data"));
        session.frames = parseFrames(readJson(directory / frames_file, "frame data"));
        return session;
    } catch (const Error& error) {
        return error;
    } catch (const Json::exception& error) {
        return Error{ErrorCode::InvalidInput, "Invalid session JSON: " + std::string{error.what()}};
    } catch (const std::ios_base::failure& error) {
        return Error{ErrorCode::IoFailure,
                     "Cannot read session data: " + std::string{error.what()}};
    }
}

Result<ReplayResult> replaySession(const Session& session, const ITerrain& terrain,
                                   const PeakCatalog& catalog) {
    auto timeline_result = PoseTimeline::create(session.poses);
    if (const auto* error = std::get_if<Error>(&timeline_result))
        return *error;
    const auto valid_camera = validateCamera(session.camera);
    if (const auto* error = std::get_if<Error>(&valid_camera))
        return *error;
    const auto valid_options = validateOptions(session.options);
    if (const auto* error = std::get_if<Error>(&valid_options))
        return *error;
    const auto valid_position = makeGeoPosition(session.observer.position.coord,
                                                session.observer.position.ellipsoid_height_m);
    if (const auto* error = std::get_if<Error>(&valid_position))
        return *error;
    if (session.max_gap.count() < 0)
        return Error{ErrorCode::InvalidInput, "Session max_gap must be non-negative"};

    const auto& timeline = std::get<PoseTimeline>(timeline_result);
    const PeakEngine engine{terrain, catalog};
    ReplayResult replay;
    replay.frames.reserve(session.frames.size());
    for (std::size_t index = 0; index < session.frames.size(); ++index) {
        const auto& frame = session.frames[index];
        ReplayFrameResult record;
        record.index = index;
        record.timestamp = frame.timestamp;
        record.image_path = frame.image_path;
        const auto sampled = timeline.sampleAt(frame.timestamp, session.max_gap);
        if (const auto* error = std::get_if<Error>(&sampled)) {
            record.error = *error;
            ++replay.skipped_count;
        } else {
            FrameQuery query{frame.timestamp, session.observer, std::get<CameraPose>(sampled),
                             session.camera, session.options};
            const auto evaluated = engine.evaluate(query);
            if (const auto* evaluation_error = std::get_if<Error>(&evaluated)) {
                record.error = *evaluation_error;
                ++replay.skipped_count;
            } else {
                record.result = std::get<FrameResult>(evaluated);
                ++replay.completed_count;
            }
        }
        replay.frames.push_back(std::move(record));
    }
    return replay;
}

Status writeReplayReportJson(const ReplayResult& replay, const Session& session,
                             const RunMetadata& metadata, const std::filesystem::path& output) {
    try {
        if (replay.frames.size() != session.frames.size() ||
            replay.completed_count + replay.skipped_count != replay.frames.size())
            return Error{ErrorCode::InvalidInput, "Replay summary counts are inconsistent"};
        Json document = {{"schema_version", 1},
                         {"metadata",
                          {{"program_version", metadata.program_version},
                           {"dataset_id", metadata.dataset_id},
                           {"dataset_sha256", metadata.dataset_sha256}}},
                         {"session",
                          {{"observer",
                            {{"latitude_deg", session.observer.position.coord.latitude_deg},
                             {"longitude_deg", session.observer.position.coord.longitude_deg},
                             {"ellipsoid_height_m", session.observer.position.ellipsoid_height_m},
                             {"simulated", session.observer.simulated}}},
                           {"camera",
                            {{"width_px", session.camera.width_px},
                             {"height_px", session.camera.height_px},
                             {"fx_px", session.camera.fx_px},
                             {"fy_px", session.camera.fy_px},
                             {"cx_px", session.camera.cx_px},
                             {"cy_px", session.camera.cy_px}}},
                           {"options", optionsJson(session.options)},
                           {"max_gap_ns", session.max_gap.count()},
                           {"pose_count", session.poses.size()},
                           {"frame_count", session.frames.size()}}},
                         {"summary",
                          {{"completed_count", replay.completed_count},
                           {"skipped_count", replay.skipped_count}}},
                         {"frames", Json::array()}};

        for (std::size_t index = 0; index < replay.frames.size(); ++index) {
            const auto& record = replay.frames[index];
            if (record.index != index || record.timestamp != session.frames[index].timestamp ||
                static_cast<bool>(record.result) == static_cast<bool>(record.error))
                return Error{ErrorCode::InvalidInput, "Replay frame records are inconsistent"};
            Json frame = {
                {"index", record.index},
                {"timestamp_ns", record.timestamp.count()},
                {"image_path", record.image_path ? Json(*record.image_path) : Json(nullptr)}};
            if (record.result) {
                frame["status"] = "completed";
                frame["result_path"] =
                    record.result_path ? Json(*record.result_path) : Json(nullptr);
                frame["overlay_path"] =
                    record.overlay_path ? Json(*record.overlay_path) : Json(nullptr);
            } else {
                frame["status"] = "skipped";
                frame["error"] = {{"code", errorCodeName(record.error->code)},
                                  {"message", record.error->message}};
            }
            document["frames"].push_back(std::move(frame));
        }
        writeJson(document, output);
        return std::monostate{};
    } catch (const Error& error) {
        return error;
    } catch (const Json::exception& error) {
        return Error{ErrorCode::InvalidInput,
                     "Cannot serialize replay report: " + std::string{error.what()}};
    } catch (const std::ios_base::failure& error) {
        return Error{ErrorCode::IoFailure,
                     "Cannot write replay report: " + std::string{error.what()}};
    }
}

Status generateSyntheticSession(const SyntheticSessionOptions& options) {
    try {
        if (!std::isfinite(options.heading_error_deg))
            return Error{ErrorCode::InvalidInput, "heading error must be finite"};
        if (options.duration.count() < 2)
            return Error{ErrorCode::InvalidInput, "duration must be at least 2 ns"};
        if (options.pose_delay.count() < 0 || options.pose_delay > options.duration)
            return Error{ErrorCode::InvalidInput, "pose delay must be between zero and duration"};
        if (options.output_directory.empty())
            return Error{ErrorCode::InvalidInput, "output directory must not be empty"};

        std::error_code filesystem_error;
        std::filesystem::create_directories(options.output_directory, filesystem_error);
        if (filesystem_error)
            return Error{ErrorCode::IoFailure,
                         "Cannot create session directory: " + filesystem_error.message()};

        const Timestamp middle{options.duration.count() / 2};
        const std::vector<Timestamp> times{Timestamp{0}, middle, options.duration};
        Json poses = Json::array();
        for (const auto time : times) {
            const double active = static_cast<double>(
                std::max<Timestamp::rep>(0, time.count() - options.pose_delay.count()));
            const double heading = options.heading_error_deg +
                                   90.0 * active / static_cast<double>(options.duration.count());
            const auto pose = checked(poseFromHeadingPitchRoll(heading, 0.0, 0.0));
            const auto& quaternion = pose.camera_from_enu;
            poses.push_back({{"timestamp_ns", time.count()},
                             {"camera_from_enu",
                              {{"w", quaternion.w()},
                               {"x", quaternion.x()},
                               {"y", quaternion.y()},
                               {"z", quaternion.z()}}}});
        }
        Json frames = Json::array();
        for (const auto time : times)
            frames.push_back({{"timestamp_ns", time.count()}});
        const Json session = {
            {"schema_version", 1},
            {"observer",
             {{"latitude_deg", 0.0},
              {"longitude_deg", 0.0},
              {"height", {{"mode", "ellipsoid"}, {"metres", 1000.0}}},
              {"simulated", true}}},
            {"camera", {{"width_px", 1280}, {"height_px", 720}, {"horizontal_fov_deg", 60.0}}},
            {"options", {{"min_range_m", 500.0}, {"max_range_m", 5000.0}}},
            {"max_gap_ns", options.duration.count()},
            {"poses_file", "poses.json"},
            {"frames_file", "frames.json"}};
        writeJson(session, options.output_directory / "session.json");
        writeJson(poses, options.output_directory / "poses.json");
        writeJson(frames, options.output_directory / "frames.json");
        return std::monostate{};
    } catch (const Error& error) {
        return error;
    } catch (const Json::exception& error) {
        return Error{ErrorCode::InvalidInput,
                     "Cannot serialize generated session: " + std::string{error.what()}};
    } catch (const std::ios_base::failure& error) {
        return Error{ErrorCode::IoFailure,
                     "Cannot write generated session: " + std::string{error.what()}};
    }
}
} // namespace tatra::desktop
