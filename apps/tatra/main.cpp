#include <iostream>
#include <string_view>
#include <tatra/version.hpp>

#if TATRA_BUILD_DESKTOP_IO
#include <filesystem>
#include <string>
#include <tatra/desktop/io.hpp>
#include <utility>

namespace {
// Fixed, explicitly synthetic terrain and catalog. Real datasets arrive in T13.
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

int render(int argc, char** argv) {
    std::string dataset;
    std::filesystem::path scene;
    std::filesystem::path output;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc)
            return report({tatra::ErrorCode::InvalidInput, "Missing option value"});
        const std::string_view flag{argv[i]};
        if (flag == "--dataset" && dataset.empty())
            dataset = argv[i + 1];
        else if (flag == "--scene" && scene.empty())
            scene = argv[i + 1];
        else if (flag == "--out" && output.empty())
            output = argv[i + 1];
        else
            return report({tatra::ErrorCode::InvalidInput,
                           "Unknown or repeated option: " + std::string{flag}});
    }
    if (dataset != "synthetic" || scene.empty() || output.empty())
        return report({tatra::ErrorCode::InvalidInput,
                       "Usage: tatra render --dataset synthetic --scene FILE --out DIRECTORY"});

    const SyntheticTerrain terrain;
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
    const auto loaded = tatra::desktop::loadScene(scene, terrain);
    if (const auto* error = std::get_if<tatra::Error>(&loaded))
        return report(*error);
    const auto& query = std::get<tatra::FrameQuery>(loaded);
    const auto evaluated = tatra::PeakEngine{terrain, catalog}.evaluate(query);
    if (const auto* error = std::get_if<tatra::Error>(&evaluated))
        return report(*error);
    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error)
        return report(
            {tatra::ErrorCode::IoFailure, "Cannot create output directory: " + error.message()});
    const tatra::desktop::RunMetadata metadata{std::string{tatra::version}, "synthetic-v1", "",
                                               query.options};
    const auto written = tatra::desktop::writeFrameJson(std::get<tatra::FrameResult>(evaluated),
                                                        catalog, metadata, output / "result.json");
    if (const auto* failure = std::get_if<tatra::Error>(&written))
        return report(*failure);
    const tatra::desktop::RenderOptions render_options{query.camera, false, std::nullopt};
    const auto svg = tatra::desktop::writeOverlaySvg(std::get<tatra::FrameResult>(evaluated),
                                                     catalog, render_options,
                                                     output / "overlay.svg");
    if (const auto* failure = std::get_if<tatra::Error>(&svg))
        return report(*failure);
    std::cout << "Written " << (output / "result.json").string() << '\n'
              << "Written " << (output / "overlay.svg").string() << '\n';
    return 0;
}
} // namespace
#endif

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help")) {
        std::cout << "TatraPeak " << tatra::version << '\n'
                  << "Usage: tatra --help | --version\n"
                  << "       tatra render --dataset synthetic --scene FILE --out DIRECTORY\n"
                  << "render writes result.json and overlay.svg using built-in synthetic data.\n"
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
    if (command == "replay" || command == "evaluate") {
        std::cerr << command << " is not implemented yet; see docs/MVP_TASKS.md.\n";
        return 2;
    }
    std::cerr << "Unknown arguments. Run tatra --help.\n";
    return 2;
}
