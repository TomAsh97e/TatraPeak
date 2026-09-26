#include <iostream>
#include <string_view>
#include <tatra/version.hpp>

#if TATRA_BUILD_DESKTOP_IO
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <tatra/desktop/io.hpp>
#include <utility>
#include <vector>

namespace {
// Fixed, explicitly synthetic terrain and catalog used only by the legacy render demo.
class SyntheticTerrain final : public tatra::ITerrain {
  public:
    tatra::HeightResult sampleHeight(tatra::GeoCoord position) const override {
        if (position.latitude_deg < -0.1 || position.latitude_deg > 0.1 ||
            position.longitude_deg < -0.1 || position.longitude_deg > 0.1)
            return tatra::TerrainSampleError::OutsideCoverage;
        if (position.latitude_deg > 0.004 && position.latitude_deg < 0.006) {
            if (position.longitude_deg > 0.0005)
                return tatra::HeightSample{2000.0};
            if (position.longitude_deg < -0.0005)
                return tatra::TerrainSampleError::NoData;
        }
        return tatra::HeightSample{0.0};
    }
};

int report(const tatra::Error& error) {
    std::cerr << error.message << '\n';
    return error.code == tatra::ErrorCode::IoFailure ? 3 : 2;
}

struct RenderCommand {
    std::string dataset;
    std::filesystem::path scene;
    std::filesystem::path output;
    std::vector<tatra::PeakId> debug_peak_ids;
};

struct ReplayCommand {
    std::string dataset;
    std::filesystem::path session;
    std::filesystem::path output;
};

template <class Number> bool parseNumber(std::string_view text, Number& value) {
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

tatra::PeakCatalog syntheticCatalog() {
    tatra::PeakCatalog catalog;
    const auto add = [&](const char* id, const char* name, double latitude, double longitude) {
        tatra::Peak peak;
        peak.id = id;
        peak.name = name;
        peak.geometry_position = {{latitude, longitude}, 1000.0};
        peak.geometry_height_source = tatra::GeometryHeightSource::VerifiedEllipsoid;
        catalog.peaks.push_back(std::move(peak));
    };
    add("test:visible", "Widoczny", 0.01, 0.0);
    add("test:outside", "Poza kadrem", 0.01, 0.02);
    add("test:behind", "Za kamerą", -0.01, 0.0);
    add("test:occluded", "Za granią", 0.01, 0.003);
    add("test:missing", "Brak terenu", 0.01, -0.003);
    catalog.peaks.front().display_elevation =
        tatra::CatalogElevation{2499.0, "fixture", "synthetic"};
    return catalog;
}

int renderScene(const RenderCommand& command, const tatra::ITerrain& terrain,
                const tatra::PeakCatalog& catalog, std::string dataset_id,
                std::string dataset_sha256) {
    const auto loaded = tatra::desktop::loadScene(command.scene, terrain);
    if (const auto* error = std::get_if<tatra::Error>(&loaded))
        return report(*error);
    const auto& query = std::get<tatra::FrameQuery>(loaded);
    const auto evaluated = tatra::PeakEngine{terrain, catalog}.evaluate(query);
    if (const auto* error = std::get_if<tatra::Error>(&evaluated))
        return report(*error);

    auto debug_ids = command.debug_peak_ids;
    std::sort(debug_ids.begin(), debug_ids.end());
    if (std::adjacent_find(debug_ids.begin(), debug_ids.end()) != debug_ids.end())
        return report({tatra::ErrorCode::InvalidInput, "Repeated --debug-peak id"});
    std::vector<tatra::desktop::VisibilityDebugRecord> debug_records;
    debug_records.reserve(debug_ids.size());
    for (const auto& id : debug_ids) {
        const auto peak =
            std::find_if(catalog.peaks.begin(), catalog.peaks.end(),
                         [&](const tatra::Peak& candidate) { return candidate.id == id; });
        if (peak == catalog.peaks.end())
            return report({tatra::ErrorCode::InvalidInput, "Unknown --debug-peak id: " + id});
        debug_records.push_back({id, tatra::traceVisibilityDebug(query.observer, *peak, terrain,
                                                                 query.options.visibility)});
    }

    std::error_code error;
    std::filesystem::create_directories(command.output, error);
    if (error)
        return report(
            {tatra::ErrorCode::IoFailure, "Cannot create output directory: " + error.message()});
    const tatra::desktop::RunMetadata metadata{std::string{tatra::version}, std::move(dataset_id),
                                               std::move(dataset_sha256), query.options,
                                               query.observer.simulated};
    const auto written = tatra::desktop::writeFrameJson(
        std::get<tatra::FrameResult>(evaluated), catalog, metadata, command.output / "result.json");
    if (const auto* failure = std::get_if<tatra::Error>(&written))
        return report(*failure);
    const tatra::desktop::RenderOptions render_options{query.camera, !debug_records.empty(),
                                                       std::nullopt};
    const auto svg =
        tatra::desktop::writeOverlaySvg(std::get<tatra::FrameResult>(evaluated), catalog,
                                        render_options, command.output / "overlay.svg");
    if (const auto* failure = std::get_if<tatra::Error>(&svg))
        return report(*failure);
    if (!debug_records.empty()) {
        const auto debug = tatra::desktop::writeVisibilityDebugJson(
            debug_records, metadata, command.output / "visibility-debug.json");
        if (const auto* failure = std::get_if<tatra::Error>(&debug))
            return report(*failure);
    }
    std::cout << "Written " << (command.output / "result.json").string() << '\n'
              << "Written " << (command.output / "overlay.svg").string() << '\n';
    if (!debug_records.empty())
        std::cout << "Written " << (command.output / "visibility-debug.json").string() << '\n';
    return 0;
}

int render(int argc, char** argv) {
    RenderCommand command;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc)
            return report({tatra::ErrorCode::InvalidInput, "Missing option value"});
        const std::string_view flag{argv[i]};
        if (flag == "--dataset" && command.dataset.empty())
            command.dataset = argv[i + 1];
        else if (flag == "--scene" && command.scene.empty())
            command.scene = argv[i + 1];
        else if (flag == "--out" && command.output.empty())
            command.output = argv[i + 1];
        else if (flag == "--debug-peak")
            command.debug_peak_ids.emplace_back(argv[i + 1]);
        else
            return report({tatra::ErrorCode::InvalidInput,
                           "Unknown or repeated option: " + std::string{flag}});
    }
    if (command.dataset.empty() || command.scene.empty() || command.output.empty())
        return report({tatra::ErrorCode::InvalidInput,
                       "Usage: tatra render --dataset DIRECTORY|synthetic --scene FILE --out "
                       "DIRECTORY [--debug-peak ID ...]"});

    if (command.dataset == "synthetic") {
        const SyntheticTerrain terrain;
        const auto catalog = syntheticCatalog();
        return renderScene(command, terrain, catalog, "synthetic-v1", "");
    }
    auto loaded_dataset = tatra::desktop::loadDataset(command.dataset);
    if (const auto* error = std::get_if<tatra::Error>(&loaded_dataset))
        return report(*error);
    const auto& dataset = std::get<tatra::desktop::Dataset>(loaded_dataset);
    return renderScene(command, dataset.terrain, dataset.catalog, dataset.metadata.dataset_id,
                       dataset.metadata.manifest_sha256);
}

int replayLoaded(const ReplayCommand& command, const tatra::ITerrain& terrain,
                 const tatra::PeakCatalog& catalog, std::string dataset_id,
                 std::string dataset_sha256) {
    const auto loaded = tatra::desktop::loadSession(command.session, terrain);
    if (const auto* error = std::get_if<tatra::Error>(&loaded))
        return report(*error);
    const auto& session = std::get<tatra::Session>(loaded);
    auto replayed = tatra::desktop::replaySession(session, terrain, catalog);
    if (const auto* error = std::get_if<tatra::Error>(&replayed))
        return report(*error);
    auto& replay = std::get<tatra::desktop::ReplayResult>(replayed);

    std::error_code error;
    std::filesystem::create_directories(command.output / "frames", error);
    if (error)
        return report(
            {tatra::ErrorCode::IoFailure, "Cannot create output directory: " + error.message()});
    const tatra::desktop::RunMetadata metadata{std::string{tatra::version}, std::move(dataset_id),
                                               std::move(dataset_sha256), session.options,
                                               session.observer.simulated};
    for (auto& frame : replay.frames) {
        if (!frame.result)
            continue;
        std::ostringstream name;
        name << std::setfill('0') << std::setw(6) << frame.index << '-' << frame.timestamp.count();
        const auto relative_directory = std::filesystem::path{"frames"} / name.str();
        const auto frame_directory = command.output / relative_directory;
        std::filesystem::create_directories(frame_directory, error);
        if (error)
            return report({tatra::ErrorCode::IoFailure,
                           "Cannot create frame output directory: " + error.message()});

        const auto result_path = relative_directory / "result.json";
        const auto written = tatra::desktop::writeFrameJson(*frame.result, catalog, metadata,
                                                            command.output / result_path);
        if (const auto* failure = std::get_if<tatra::Error>(&written))
            return report(*failure);
        std::optional<std::filesystem::path> background;
        if (frame.image_path)
            background = std::filesystem::absolute(command.session / *frame.image_path, error);
        if (error)
            return report({tatra::ErrorCode::IoFailure,
                           "Cannot resolve frame image path: " + error.message()});
        const auto overlay_path = relative_directory / "overlay.svg";
        const tatra::desktop::RenderOptions render_options{session.camera, false, background};
        const auto svg = tatra::desktop::writeOverlaySvg(*frame.result, catalog, render_options,
                                                         command.output / overlay_path);
        if (const auto* failure = std::get_if<tatra::Error>(&svg))
            return report(*failure);
        frame.result_path = result_path.generic_string();
        frame.overlay_path = overlay_path.generic_string();
    }
    const auto report_status = tatra::desktop::writeReplayReportJson(
        replay, session, metadata, command.output / "report.json");
    if (const auto* failure = std::get_if<tatra::Error>(&report_status))
        return report(*failure);
    std::cout << "Replay completed: " << replay.completed_count
              << ", skipped: " << replay.skipped_count << '\n'
              << "Written " << (command.output / "report.json").string() << '\n';
    return 0;
}

int replay(int argc, char** argv) {
    ReplayCommand command;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc)
            return report({tatra::ErrorCode::InvalidInput, "Missing option value"});
        const std::string_view flag{argv[i]};
        if (flag == "--dataset" && command.dataset.empty())
            command.dataset = argv[i + 1];
        else if (flag == "--session" && command.session.empty())
            command.session = argv[i + 1];
        else if (flag == "--out" && command.output.empty())
            command.output = argv[i + 1];
        else
            return report({tatra::ErrorCode::InvalidInput,
                           "Unknown or repeated option: " + std::string{flag}});
    }
    if (command.dataset.empty() || command.session.empty() || command.output.empty())
        return report({tatra::ErrorCode::InvalidInput,
                       "Usage: tatra replay --dataset DIRECTORY|synthetic --session DIRECTORY "
                       "--out DIRECTORY"});
    if (command.dataset == "synthetic") {
        const SyntheticTerrain terrain;
        const auto catalog = syntheticCatalog();
        return replayLoaded(command, terrain, catalog, "synthetic-v1", "");
    }
    auto loaded_dataset = tatra::desktop::loadDataset(command.dataset);
    if (const auto* error = std::get_if<tatra::Error>(&loaded_dataset))
        return report(*error);
    const auto& dataset = std::get<tatra::desktop::Dataset>(loaded_dataset);
    return replayLoaded(command, dataset.terrain, dataset.catalog, dataset.metadata.dataset_id,
                        dataset.metadata.manifest_sha256);
}

int generateSession(int argc, char** argv) {
    tatra::desktop::SyntheticSessionOptions options;
    bool heading_error_set = false;
    bool pose_delay_set = false;
    bool duration_set = false;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc)
            return report({tatra::ErrorCode::InvalidInput, "Missing option value"});
        const std::string_view flag{argv[i]};
        const std::string_view value{argv[i + 1]};
        if (flag == "--out" && options.output_directory.empty()) {
            options.output_directory = argv[i + 1];
        } else if (flag == "--heading-error-deg" && !heading_error_set) {
            if (!parseNumber(value, options.heading_error_deg))
                return report(
                    {tatra::ErrorCode::InvalidInput, "--heading-error-deg must be a number"});
            heading_error_set = true;
        } else if (flag == "--pose-delay-ns" && !pose_delay_set) {
            tatra::Timestamp::rep parsed = 0;
            if (!parseNumber(value, parsed))
                return report(
                    {tatra::ErrorCode::InvalidInput, "--pose-delay-ns must be an integer"});
            options.pose_delay = tatra::Timestamp{parsed};
            pose_delay_set = true;
        } else if (flag == "--duration-ns" && !duration_set) {
            tatra::Timestamp::rep parsed = 0;
            if (!parseNumber(value, parsed))
                return report({tatra::ErrorCode::InvalidInput, "--duration-ns must be an integer"});
            options.duration = tatra::Timestamp{parsed};
            duration_set = true;
        } else {
            return report({tatra::ErrorCode::InvalidInput,
                           "Unknown or repeated option: " + std::string{flag}});
        }
    }
    if (options.output_directory.empty())
        return report({tatra::ErrorCode::InvalidInput,
                       "Usage: tatra generate-session --out DIRECTORY [--heading-error-deg DEG] "
                       "[--pose-delay-ns NS] [--duration-ns NS]"});
    const auto generated = tatra::desktop::generateSyntheticSession(options);
    if (const auto* error = std::get_if<tatra::Error>(&generated))
        return report(*error);
    std::cout << "Written synthetic session to " << options.output_directory.string() << '\n';
    return 0;
}

int datasetInfo(int argc, char** argv) {
    if (argc != 4 || std::string_view{argv[2]} != "--dataset")
        return report(
            {tatra::ErrorCode::InvalidInput, "Usage: tatra dataset-info --dataset DIRECTORY"});
    auto loaded = tatra::desktop::loadDataset(argv[3]);
    if (const auto* error = std::get_if<tatra::Error>(&loaded))
        return report(*error);
    const auto& dataset = std::get<tatra::desktop::Dataset>(loaded);
    const auto& metadata = dataset.metadata;
    std::cout << "dataset_id: " << metadata.dataset_id << '\n'
              << "manifest_sha256: " << metadata.manifest_sha256 << '\n'
              << "terrain: " << metadata.terrain_columns << 'x' << metadata.terrain_rows << '\n'
              << "peaks: " << metadata.peak_count << '\n'
              << "horizontal_reference: " << metadata.horizontal_reference << '\n'
              << "vertical_reference: " << metadata.vertical_reference << '\n'
              << "source_vertical_reference: " << metadata.source_vertical_reference << '\n'
              << "geoid_model: " << metadata.geoid_model << '\n';
    return 0;
}
} // namespace
#endif

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help")) {
        std::cout
            << "TatraPeak " << tatra::version << '\n'
            << "Usage: tatra --help | --version\n"
            << "       tatra render --dataset DIRECTORY|synthetic --scene FILE --out DIRECTORY\\\n"
            << "           [--debug-peak ID ...]\n"
            << "       tatra dataset-info --dataset DIRECTORY\n"
            << "       tatra replay --dataset DIRECTORY|synthetic --session DIRECTORY --out "
               "DIRECTORY\n"
            << "       tatra generate-session --out DIRECTORY [--heading-error-deg DEG]\n"
            << "           [--pose-delay-ns NS] [--duration-ns NS]\n"
            << "render writes result.json and overlay.svg; selected line-of-sight traces\n"
            << "are written to visibility-debug.json.\n"
            << "replay writes report.json and one result/overlay pair per completed frame.\n"
            << "generate-session writes a controlled north-to-east rotation fixture.\n"
            << "dataset-info validates a package and prints its summary.\n"
            << "Requires TATRA_BUILD_DESKTOP_IO=ON.\n"
            << "Exit codes: 0 success, 2 invalid input, 3 I/O failure.\n";
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << tatra::version << '\n';
        return 0;
    }
    const std::string_view command{argv[1]};
    if (command == "render") {
#if TATRA_BUILD_DESKTOP_IO
        return render(argc, argv);
#else
        std::cerr << "render requires a build with TATRA_BUILD_DESKTOP_IO=ON.\n";
        return 2;
#endif
    }
    if (command == "dataset-info") {
#if TATRA_BUILD_DESKTOP_IO
        return datasetInfo(argc, argv);
#else
        std::cerr << "dataset-info requires a build with TATRA_BUILD_DESKTOP_IO=ON.\n";
        return 2;
#endif
    }
    if (command == "replay") {
#if TATRA_BUILD_DESKTOP_IO
        return replay(argc, argv);
#else
        std::cerr << "replay requires a build with TATRA_BUILD_DESKTOP_IO=ON.\n";
        return 2;
#endif
    }
    if (command == "generate-session") {
#if TATRA_BUILD_DESKTOP_IO
        return generateSession(argc, argv);
#else
        std::cerr << "generate-session requires a build with TATRA_BUILD_DESKTOP_IO=ON.\n";
        return 2;
#endif
    }
    if (command == "evaluate") {
        std::cerr << command << " is not implemented yet; see docs/MVP_TASKS.md.\n";
        return 2;
    }
    std::cerr << "Unknown arguments. Run tatra --help.\n";
    return 2;
}
