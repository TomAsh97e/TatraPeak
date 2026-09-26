#include <charconv>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <tatra/version.hpp>

#if TATRA_BUILD_DESKTOP_IO
#include <tatra/desktop/io.hpp>
#endif

namespace {

void printHelp() {
    std::cout << "TatraPeak data preparation " << tatra::version << '\n'
              << "Usage: tatra-prepare --help | --version\n"
              << "       tatra-prepare dem --raw-dem FILE [--raw-dem FILE ...]\\\n"
              << "           --geoid-dir DIRECTORY [--geoid-model NAME]\\\n"
              << "           --bounds WEST SOUTH EAST NORTH --out DIRECTORY\n"
              << "       tatra-prepare peaks --raw-osm FILE --terrain FILE\\\n"
              << "           --selection FILE --corrections FILE --out DIRECTORY\n"
              << "       tatra-prepare manifest --dataset-id ID --terrain FILE --peaks FILE\\\n"
              << "           --source-metadata FILE --attribution FILE --out DIRECTORY\n"
              << "The dem command mosaics and crops the natural WGS84 grid, then converts\n"
              << "EGM2008 orthometric heights to WGS84 ellipsoid heights.\n"
              << "The peaks command imports a reviewed OSM selection and samples geometry\n"
              << "heights from the normalized terrain.\n"
              << "The manifest command validates and seals a reproducible dataset package.\n";
}

#if TATRA_BUILD_DESKTOP_IO
int report(const tatra::Error& error) {
    std::cerr << error.message << '\n';
    return error.code == tatra::ErrorCode::IoFailure ? 3 : 2;
}

bool parseDouble(std::string_view text, double& value) {
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end && std::isfinite(value);
}

int prepareDem(int argc, char** argv) {
    tatra::desktop::PrepareOptions options;
    bool has_bounds = false;
    bool has_geoid_model = false;
    for (int index = 2; index < argc;) {
        const std::string_view flag{argv[index]};
        if (flag == "--raw-dem") {
            if (index + 1 >= argc)
                return report({tatra::ErrorCode::InvalidInput, "Missing --raw-dem value"});
            options.raw_dem.emplace_back(argv[index + 1]);
            index += 2;
        } else if (flag == "--geoid-dir" && options.geoid_directory.empty()) {
            if (index + 1 >= argc)
                return report({tatra::ErrorCode::InvalidInput, "Missing --geoid-dir value"});
            options.geoid_directory = argv[index + 1];
            index += 2;
        } else if (flag == "--geoid-model" && !has_geoid_model) {
            if (index + 1 >= argc)
                return report({tatra::ErrorCode::InvalidInput, "Missing --geoid-model value"});
            options.geoid_model = argv[index + 1];
            has_geoid_model = true;
            index += 2;
        } else if (flag == "--out" && options.output_directory.empty()) {
            if (index + 1 >= argc)
                return report({tatra::ErrorCode::InvalidInput, "Missing --out value"});
            options.output_directory = argv[index + 1];
            index += 2;
        } else if (flag == "--bounds" && !has_bounds) {
            if (index + 4 >= argc)
                return report(
                    {tatra::ErrorCode::InvalidInput, "--bounds requires WEST SOUTH EAST NORTH"});
            auto& bounds = options.bounds;
            if (!parseDouble(argv[index + 1], bounds.west_deg) ||
                !parseDouble(argv[index + 2], bounds.south_deg) ||
                !parseDouble(argv[index + 3], bounds.east_deg) ||
                !parseDouble(argv[index + 4], bounds.north_deg))
                return report(
                    {tatra::ErrorCode::InvalidInput, "--bounds values must be finite numbers"});
            has_bounds = true;
            index += 5;
        } else {
            return report({tatra::ErrorCode::InvalidInput,
                           "Unknown or repeated option: " + std::string{flag}});
        }
    }
    if (options.raw_dem.empty() || options.geoid_directory.empty() ||
        options.output_directory.empty() || !has_bounds)
        return report({tatra::ErrorCode::InvalidInput,
                       "dem requires --raw-dem, --geoid-dir, --bounds and --out"});
    const auto result = tatra::desktop::prepareDataset(options);
    if (const auto* error = std::get_if<tatra::Error>(&result))
        return report(*error);
    std::cout << "Written " << (options.output_directory / "terrain-ellipsoid.tif").string()
              << '\n';
    return 0;
}

int preparePeaks(int argc, char** argv) {
    tatra::desktop::PeakPrepareOptions options;
    for (int index = 2; index < argc;) {
        const std::string_view flag{argv[index]};
        if (index + 1 >= argc)
            return report(
                {tatra::ErrorCode::InvalidInput, "Missing value for option: " + std::string{flag}});
        const std::filesystem::path value{argv[index + 1]};
        if (flag == "--raw-osm" && options.raw_osm.empty())
            options.raw_osm = value;
        else if (flag == "--terrain" && options.terrain.empty())
            options.terrain = value;
        else if (flag == "--selection" && options.selection.empty())
            options.selection = value;
        else if (flag == "--corrections" && options.corrections.empty())
            options.corrections = value;
        else if (flag == "--out" && options.output_directory.empty())
            options.output_directory = value;
        else
            return report({tatra::ErrorCode::InvalidInput,
                           "Unknown or repeated option: " + std::string{flag}});
        index += 2;
    }
    if (options.raw_osm.empty() || options.terrain.empty() || options.selection.empty() ||
        options.corrections.empty() || options.output_directory.empty())
        return report(
            {tatra::ErrorCode::InvalidInput,
             "peaks requires --raw-osm, --terrain, --selection, --corrections and --out"});
    auto result = tatra::desktop::preparePeakCatalog(options);
    if (const auto* error = std::get_if<tatra::Error>(&result))
        return report(*error);
    const auto& import_report = std::get<tatra::desktop::PeakImportReport>(result);
    std::cout << "Written " << (options.output_directory / "peaks.json").string()
              << " (accepted=" << import_report.accepted_count
              << ", rejected=" << import_report.rejected_count
              << ", warnings=" << import_report.missing_ele_count + import_report.invalid_ele_count
              << ")\n";
    return 0;
}

int prepareManifest(int argc, char** argv) {
    tatra::desktop::DatasetManifestOptions options;
    for (int index = 2; index < argc;) {
        const std::string_view flag{argv[index]};
        if (index + 1 >= argc)
            return report(
                {tatra::ErrorCode::InvalidInput, "Missing value for option: " + std::string{flag}});
        const std::string_view value{argv[index + 1]};
        if (flag == "--dataset-id" && options.dataset_id.empty())
            options.dataset_id = value;
        else if (flag == "--terrain" && options.terrain.empty())
            options.terrain = value;
        else if (flag == "--peaks" && options.peaks.empty())
            options.peaks = value;
        else if (flag == "--source-metadata" && options.source_metadata.empty())
            options.source_metadata = value;
        else if (flag == "--attribution" && options.attribution.empty())
            options.attribution = value;
        else if (flag == "--out" && options.output_directory.empty())
            options.output_directory = value;
        else
            return report({tatra::ErrorCode::InvalidInput,
                           "Unknown or repeated option: " + std::string{flag}});
        index += 2;
    }
    if (options.dataset_id.empty() || options.terrain.empty() || options.peaks.empty() ||
        options.source_metadata.empty() || options.attribution.empty() ||
        options.output_directory.empty())
        return report({tatra::ErrorCode::InvalidInput,
                       "manifest requires --dataset-id, --terrain, --peaks, --source-metadata, "
                       "--attribution and --out"});
    const auto result = tatra::desktop::writeDatasetManifest(options);
    if (const auto* error = std::get_if<tatra::Error>(&result))
        return report(*error);
    std::cout << "Written " << (options.output_directory / "manifest.json").string() << '\n'
              << "Written " << (options.output_directory / "ATTRIBUTION.txt").string() << '\n';
    return 0;
}
#endif

} // namespace

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help")) {
        printHelp();
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << tatra::version << '\n';
        return 0;
    }
    if (std::string_view{argv[1]} == "dem") {
#if TATRA_BUILD_DESKTOP_IO
        return prepareDem(argc, argv);
#else
        std::cerr << "dem requires a build with TATRA_BUILD_DESKTOP_IO=ON.\n";
        return 2;
#endif
    }
    if (std::string_view{argv[1]} == "peaks") {
#if TATRA_BUILD_DESKTOP_IO
        return preparePeaks(argc, argv);
#else
        std::cerr << "peaks requires a build with TATRA_BUILD_DESKTOP_IO=ON.\n";
        return 2;
#endif
    }
    if (std::string_view{argv[1]} == "manifest") {
#if TATRA_BUILD_DESKTOP_IO
        return prepareManifest(argc, argv);
#else
        std::cerr << "manifest requires a build with TATRA_BUILD_DESKTOP_IO=ON.\n";
        return 2;
#endif
    }
    std::cerr << "Unknown command. Run tatra-prepare --help.\n";
    return 2;
}
