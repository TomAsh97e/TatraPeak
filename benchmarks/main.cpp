#include "build_info.hpp"

#include <tatra/desktop/io.hpp>
#include <tatra/version.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>
#ifdef __linux__
#include <sched.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#endif

namespace {
using namespace tatra;
using namespace tatra::desktop;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
volatile double result_sink = 0.0;

template <class T> T checked(Result<T> result) {
    if (const auto* error = std::get_if<Error>(&result))
        throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}

double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

Json loadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot read " + path.string());
    return Json::parse(input);
}

struct Arguments {
    bool synthetic = false;
    std::filesystem::path dataset, scene, output, reference;
    std::size_t batches = 31, iterations = 20, warmup = 3;
};

Arguments parse(int argc, char** argv) {
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--synthetic") {
            args.synthetic = true;
            continue;
        }
        if (++i == argc)
            throw std::runtime_error("Missing value for " + key);
        const std::string value = argv[i];
        if (key == "--dataset")
            args.dataset = value;
        else if (key == "--scene")
            args.scene = value;
        else if (key == "--out")
            args.output = value;
        else if (key == "--reference")
            args.reference = value;
        else if (key == "--batches" || key == "--iterations" || key == "--warmup") {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("Expected positive integer for " + key);
            const auto count = std::stoull(value);
            if (count == 0 || count > 10000)
                throw std::runtime_error("Benchmark counts must be in [1, 10000]");
            if (key == "--batches")
                args.batches = static_cast<std::size_t>(count);
            else if (key == "--iterations")
                args.iterations = static_cast<std::size_t>(count);
            else
                args.warmup = static_cast<std::size_t>(count);
        } else
            throw std::runtime_error("Unknown option " + key);
    }
    if (args.output.empty() || (!args.synthetic && (args.dataset.empty() || args.scene.empty())) ||
        (args.synthetic && (!args.dataset.empty() || !args.scene.empty())))
        throw std::runtime_error("Use --synthetic OR --dataset DIR --scene FILE, plus --out DIR");
    return args;
}

Json environment() {
    Json result = {{"logical_cpus", std::thread::hardware_concurrency()}};
#ifdef __linux__
    struct utsname system {};
    if (uname(&system) == 0)
        result["os"] = std::string(system.sysname) + " " + system.release + " " + system.machine;
    for (const auto& [path, prefix, key] :
         std::vector<std::tuple<std::string, std::string, std::string>>{
             {"/proc/cpuinfo", "model name", "cpu"}, {"/proc/meminfo", "MemTotal:", "ram"}}) {
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) {
            if (line.starts_with(prefix)) {
                result[key] = line.substr(line.find(':') + 1);
                break;
            }
        }
    }
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0) {
        result["cpu_affinity"] = Json::array();
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &allowed))
                result["cpu_affinity"].push_back(cpu);
    }
#else
    result["os"] = "not collected on this platform; supply OS/CPU/RAM in report";
#endif
    return result;
}

Json peakRssKib() {
#ifdef __linux__
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return usage.ru_maxrss;
#endif
    return nullptr;
}

template <class Operation>
Json measure(const Arguments& args, std::size_t iterations, std::size_t operations_per_iteration,
             Operation operation) {
    const auto warmup_start = Clock::now();
    for (std::size_t batch = 0; batch < args.warmup; ++batch) {
        double checksum = 0;
        for (std::size_t i = 0; i < iterations; ++i)
            checksum += operation();
        result_sink = checksum;
    }
    const double warmup_ms = milliseconds(warmup_start);
    std::vector<double> times;
    times.reserve(args.batches);
    for (std::size_t batch = 0; batch < args.batches; ++batch) {
        double checksum = 0;
        const auto start = Clock::now();
        for (std::size_t i = 0; i < iterations; ++i)
            checksum += operation();
        const auto end = Clock::now();
        result_sink = checksum; // Observable use; no output/JSON/clock calls inside the inner loop.
        times.push_back(std::chrono::duration<double, std::nano>(end - start).count() /
                        static_cast<double>(iterations * operations_per_iteration));
    }
    const auto raw = times;
    std::sort(times.begin(), times.end());
    const auto percentile = [&](double fraction) {
        return times[static_cast<std::size_t>(
                         std::ceil(fraction * static_cast<double>(times.size()))) -
                     1];
    };
    return {{"unit", "ns/operation"},
            {"median", percentile(.5)},
            {"p95", percentile(.95)},
            {"min", times.front()},
            {"max", times.back()},
            {"batches", args.batches},
            {"iterations_per_batch", iterations},
            {"operations_per_iteration", operations_per_iteration},
            {"warmup_batches", args.warmup},
            {"warmup_ms", warmup_ms},
            {"raw_ns_per_operation", raw}};
}

GridTerrain syntheticTerrain(bool obstacle) {
    const GridGeometry geometry{101, 101, 49.21, 19.99, -0.0002, 0.0002};
    std::vector<float> heights(101 * 101, 900.0F);
    if (obstacle) {
        // A narrow ridge about 45-90 m north of the observer; endpoints stay clear.
        for (std::size_t row = 46; row <= 48; ++row)
            std::fill_n(heights.begin() + static_cast<std::ptrdiff_t>(row * 101), 101, 1700.0F);
    }
    return checked(GridTerrain::create(geometry, std::move(heights),
                                       std::vector<std::uint8_t>(101 * 101, 255)));
}

Dataset syntheticDataset() {
    DatasetMetadata metadata;
    metadata.dataset_id = "benchmark-synthetic-v1";
    metadata.manifest_sha256 = "built-in deterministic fixture";
    metadata.terrain_rows = metadata.terrain_columns = 101;
    PeakCatalog catalog;
    for (int i = 0; i < 53; ++i) {
        Peak peak;
        peak.id = "synthetic:" + std::to_string(i);
        peak.name = peak.id;
        peak.geometry_position = {{49.202 + static_cast<double>(i % 8) * .0007,
                                   19.994 + static_cast<double>(i % 13) * .001},
                                  1100.0};
        peak.geometry_height_source = GeometryHeightSource::VerifiedEllipsoid;
        catalog.peaks.push_back(peak);
    }
    metadata.peak_count = catalog.peaks.size();
    return {metadata, std::move(catalog), syntheticTerrain(false)};
}

Json optionalNumber(const std::optional<double>& value) {
    return value ? Json(*value) : Json(nullptr);
}
Json projectionJson(const Projection& p) {
    return {{"state", static_cast<int>(p.state)},
            {"depth_m", p.camera_depth_m},
            {"pixel", p.pixel ? Json::array({p.pixel->u, p.pixel->v}) : Json(nullptr)}};
}
Json visibilityJson(const VisibilityResult& v) {
    return {{"state", static_cast<int>(v.state)},
            {"clearance_m", optionalNumber(v.min_sampled_clearance_m)},
            {"blocker_m", optionalNumber(v.first_blocker_distance_m)},
            {"samples", v.sample_count},
            {"reason", v.reason}};
}
Json frameJson(const FrameResult& result) {
    Json points = Json::array();
    for (const auto& o : result.observations)
        points.push_back(
            {{"id", o.id},
             {"direction",
              {o.direction.azimuth_deg, o.direction.elevation_deg, o.direction.slant_range_m}},
             {"projection", projectionJson(o.projection)},
             {"visibility", o.visibility ? visibilityJson(*o.visibility) : Json(nullptr)}});
    return {{"timestamp_ns", result.timestamp.count()},
            {"observations", std::move(points)},
            {"diagnostics",
             {result.diagnostics.catalog_count, result.diagnostics.outside_range_count,
              result.diagnostics.projected_count, result.diagnostics.visibility_sample_count}}};
}

int execute(const Arguments& args) {
    const auto load_start = Clock::now();
    auto dataset = args.synthetic ? syntheticDataset() : checked(loadDataset(args.dataset));
    FrameQuery query;
    if (args.synthetic) {
        query.observer.position = {{49.2, 20.0}, 1000};
        query.camera = checked(cameraFromHorizontalFov(1280, 720, 90));
        query.pose = checked(poseFromHeadingPitchRoll(0, 0, 0));
        query.options.min_range_m = 20;
        query.options.max_range_m = 10000;
    } else
        query = checked(loadScene(args.scene, dataset.terrain));
    const double load_ms = milliseconds(load_start);
    if (dataset.catalog.peaks.empty())
        throw std::runtime_error("Benchmark requires a nonempty catalog");
    const PeakEngine engine{dataset.terrain, dataset.catalog};
    const LocalFrame local{query.observer.position};
    std::vector<EnuPoint> points;
    for (const auto& peak : dataset.catalog.peaks)
        points.push_back(local.toEnu(peak.geometry_position));
    std::vector<GeoCoord> coordinates;
    for (int row = 0; row < 64; ++row)
        for (int col = 0; col < 64; ++col)
            coordinates.push_back(
                {query.observer.position.coord.latitude_deg + (row - 32) * .0001,
                 query.observer.position.coord.longitude_deg + (col - 32) * .0001});

    auto flat = syntheticTerrain(false);
    auto ridge = syntheticTerrain(true);
    Observer trace_observer;
    trace_observer.position = {{49.2, 20}, 1000};
    Peak trace_peak;
    trace_peak.id = "synthetic-north-890m";
    trace_peak.geometry_position = {{49.208, 20}, 1000};
    VisibilityOptions trace_options;
    const auto visible = traceVisibility(trace_observer, trace_peak, flat, trace_options);
    const auto blocked = traceVisibility(trace_observer, trace_peak, ridge, trace_options);
    if (visible.state != VisibilityState::VisibleInModel ||
        blocked.state != VisibilityState::OccludedInModel || !blocked.first_blocker_distance_m ||
        *blocked.first_blocker_distance_m > 100)
        throw std::runtime_error(
            "Controlled visibility fixture does not satisfy its benchmark contract");

    Json semantics;
    semantics["project"] = Json::array();
    for (const auto& point : points)
        semantics["project"].push_back(
            projectionJson(project(point, query.pose, query.camera, query.options.near_z_m)));
    semantics["sample_height"] = Json::array();
    for (const auto coord : coordinates) {
        const auto sample = dataset.terrain.sampleHeight(coord);
        if (const auto* height = std::get_if<HeightSample>(&sample))
            semantics["sample_height"].push_back(height->ellipsoid_height_m);
        else
            semantics["sample_height"].push_back(
                {{"error", static_cast<int>(std::get<TerrainSampleError>(sample))}});
    }
    semantics["trace_visible"] = visibilityJson(visible);
    semantics["trace_early_blocker"] = visibilityJson(blocked);
    semantics["evaluate"] = frameJson(checked(engine.evaluate(query)));
    const Peak* scene_visible = nullptr;
    for (const auto& observation : checked(engine.evaluate(query)).observations) {
        if (observation.visibility &&
            observation.visibility->state == VisibilityState::VisibleInModel) {
            const auto peak =
                std::find_if(dataset.catalog.peaks.begin(), dataset.catalog.peaks.end(),
                             [&](const Peak& p) { return p.id == observation.id; });
            if (!scene_visible || observation.id == "osm:node:605282386")
                scene_visible = &*peak;
            if (observation.id == "osm:node:605282386")
                break;
        }
    }
    if (scene_visible)
        semantics["trace_scene_visible"] = visibilityJson(traceVisibility(
            query.observer, *scene_visible, dataset.terrain, query.options.visibility));
    semantics["orientation_sequence"] = Json::array();
    // T16-style data timestamps; no wall-clock time or I/O in analysis.
    for (int index = -4; index <= 4; ++index) {
        auto frame = query;
        frame.timestamp = Timestamp{(index + 4) * 100000000LL};
        frame.pose.camera_from_enu =
            query.pose.camera_from_enu *
            Eigen::Quaterniond{
                Eigen::AngleAxisd{static_cast<double>(index) * .1, Eigen::Vector3d::UnitZ()}};
        semantics["orientation_sequence"].push_back(frameJson(checked(engine.evaluate(frame))));
    }
    Json workload = {
        {"dataset_id", dataset.metadata.dataset_id},
        {"manifest_sha256", dataset.metadata.manifest_sha256},
        {"catalog_count", dataset.catalog.peaks.size()},
        {"raster_shape", {dataset.metadata.terrain_rows, dataset.metadata.terrain_columns}},
        {"scene", args.synthetic ? Json("built-in synthetic-v1") : loadJson(args.scene)},
        {"project_points",
         "all catalog points in fixed catalog order, transformed once before timing"},
        {"sample_coordinates",
         "64x64 at observer + (row-32,col-32)*0.0001 deg; fixed row-major order"},
        {"visibility_fixture",
         "synthetic northward ~890m ray, height 1000, DEM 900; optional early ridge 1700"}};
    workload["scene_visible_peak_id"] = scene_visible ? Json(scene_visible->id) : Json(nullptr);
    if (!args.reference.empty()) {
        const auto reference = loadJson(args.reference);
        if (reference.at("workload") != workload || reference.at("semantics") != semantics)
            throw std::runtime_error("Reference mismatch: workload or numerical results changed");
    }
    Json measurements;
    measurements["project"] = measure(args, args.iterations * 500, points.size(), [&] {
        double checksum = 0;
        for (const auto& point : points) {
            const auto p = project(point, query.pose, query.camera, query.options.near_z_m);
            checksum += p.camera_depth_m + (p.pixel ? p.pixel->u + p.pixel->v : 0);
        }
        return checksum;
    });
    measurements["sampleHeight"] = measure(args, args.iterations * 5, coordinates.size(), [&] {
        double checksum = 0;
        for (const auto coord : coordinates) {
            const auto result = dataset.terrain.sampleHeight(coord);
            if (const auto* height = std::get_if<HeightSample>(&result))
                checksum += height->ellipsoid_height_m;
        }
        return checksum;
    });
    measurements["traceVisibility_visible"] = measure(args, args.iterations * 5, 1, [&] {
        const auto v = traceVisibility(trace_observer, trace_peak, flat, trace_options);
        return static_cast<double>(v.sample_count) + v.min_sampled_clearance_m.value_or(0);
    });
    measurements["traceVisibility_early_blocker"] = measure(args, args.iterations * 500, 1, [&] {
        const auto v = traceVisibility(trace_observer, trace_peak, ridge, trace_options);
        return static_cast<double>(v.sample_count) + v.min_sampled_clearance_m.value_or(0);
    });
    if (scene_visible)
        measurements["traceVisibility_scene_visible"] = measure(args, args.iterations * 5, 1, [&] {
            const auto v = traceVisibility(query.observer, *scene_visible, dataset.terrain,
                                           query.options.visibility);
            return static_cast<double>(v.sample_count) + v.min_sampled_clearance_m.value_or(0);
        });
    measurements["evaluate"] = measure(args, args.iterations, 1, [&] {
        const auto frame = checked(engine.evaluate(query));
        return static_cast<double>(frame.observations.size() +
                                   frame.diagnostics.visibility_sample_count);
    });
    const Json memory = {
        {"grid_object_bytes", sizeof(GridTerrain)},
        {"raster_payload_bytes", dataset.metadata.terrain_rows * dataset.metadata.terrain_columns *
                                     (sizeof(float) + sizeof(std::uint8_t))},
        {"process_peak_rss_kib", peakRssKib()},
        {"rss_scope", "whole process including GDAL loading, fixtures and semantic JSON; not "
                      "allocation count of evaluate"}};
    Json report = {{"schema_version", 1},
                   {"program_version", tatra::version},
                   {"build",
                    {{"type", benchmarkBuildType},
                     {"compiler", benchmarkCompiler},
                     {"flags", benchmarkFlags},
                     {"configured_git_revision", benchmarkRevision},
                     {"ndebug",
#ifdef NDEBUG
                      true
#else
                      false
#endif
                     }}},
                   {"environment", environment()},
                   {"workload", workload},
                   {"memory", memory},
                   {"load_files_ms", load_ms},
                   {"measurements", measurements},
                   {"semantics", semantics},
                   {"reference_equal", args.reference.empty() ? Json(nullptr) : Json(true)},
                   {"timing_scope", "warmup separated; input loading, semantic verification, JSON "
                                    "and SVG serialization excluded; no SVG generated"}};
    std::filesystem::create_directories(args.output);
    std::ofstream output(args.output / "report.json");
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << report.dump(2) << '\n';
    for (const auto& [name, result] : measurements.items())
        std::cout << name << ": median=" << result["median"] << " p95=" << result["p95"]
                  << " ns/op\n";
    std::cout << "Report: " << (args.output / "report.json").string() << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "tatra-benchmark (--synthetic | --dataset DIR --scene FILE) --out DIR\n"
                     "  [--batches 31] [--iterations 20] [--warmup 3] [--reference report.json]\n";
        return 0;
    }
    try {
        return execute(parse(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
