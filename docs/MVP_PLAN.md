**TatraPeak: platform-independent C++20 MVP plan.**

This is the original architecture plan, maintained alongside the working
implementation. The current scope and explicitly unfinished items are described
in [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md). Detailed tasks and
completion criteria are listed in [MVP_TASKS.md](MVP_TASKS.md).

The MVP calculates which known peaks should appear in a camera image for a given
position, orientation, and set of camera parameters. It returns label coordinates,
peak data, and an assessment of terrain-model occlusion. The first inputs come
from files. All calculations run without a phone and, once the data has been
prepared, without internet access.

Use the term “geometric identification”: this version does not recognize an
arbitrary photo without information about where it was taken and in which
direction. AI, automatic panorama alignment, phone SDKs, raw IMU filtering, GPU
use, and multithreading are extensions to consider after completing this version.

**The MVP produces three tools that share code.**

| Component | Input | Output |
|---|---|---|
| tatra_core | In-memory terrain, catalog, observer, camera, and orientation data | Frame analysis result |
| tatra | Regional package and a JSON scenario or session | Result JSON, SVG, evaluation report |
| tatra-prepare | Raw DEM, geoid model, and OSM export | Normalized regional package with metadata |

Example target invocations:

~~~sh
tatra render --dataset data/tatras-v1 --scene examples/scene.json --out out/scene
tatra replay --dataset data/tatras-v1 --session examples/session --out out/replay
tatra evaluate --predictions out/scene/result.json --annotations examples/annotations.json --out out/metrics.json
~~~

Render writes `result.json` and `overlay.svg`. The SVG can be opened in a browser.
When used with a photo, it expects an ordinary perspective image with matching
dimensions; the photo is an optional background. A video recording is not
required for the first replay: frame timestamps and orientation samples are enough.

**Dependencies serve specific layers.**

| Dependency | Use |
|---|---|
| C++20, CMake, CTest | Build the library, applications, and tests |
| Eigen | Vectors, matrices, and quaternions |
| GeographicLib | WGS84/ECEF/ENU; geoids in the preparation tool |
| GDAL | GeoTIFF reading and preparation in the desktop adapter only |
| nlohmann/json | JSON file adapter only |
| GoogleTest | Function, integration, and input-error tests |
| Google Benchmark | Geometry and visibility measurements after a working version exists |

OpenCV is not required for pinhole projection or SVG generation. It may later be
used in a separate tool for camera calibration or image rectification.

Pin verified dependency versions during implementation. `tatra_core` may depend
on Eigen and GeographicLib, but not on GDAL, JSON, the file system, a GUI, or a
phone SDK. Provide the CMake option `TATRA_BUILD_DESKTOP_IO=OFF` so the core and
synthetic tests can be built without GDAL.

**The dependency flow is one-way.**

~~~mermaid
flowchart TD
    RAW["Raw DEM + OSM + geoid"] --> PREP["tatra-prepare"]
    PREP --> PACK["Regional package"]
    PACK --> IO["tatra_io: reading and validation"]
    SCENE["JSON scene / session"] --> IO
    IO --> OWN["Application: data owner"]
    OWN --> ENGINE["PeakEngine"]
    ENGINE --> GEO["geo + camera"]
    ENGINE --> VIS["visibility"]
    VIS --> TERRAIN["ITerrain / GridTerrain"]
    ENGINE --> RESULT["FrameResult"]
    RESULT --> SVG["SVG / JSON / evaluation"]
    SYNTH["Synthetic test data"] --> ENGINE
~~~

Proposed file layout:

~~~text
TatraPeak/
  CMakeLists.txt
  CMakePresets.json
  include/tatra/
    types.hpp
    geo.hpp
    camera.hpp
    terrain.hpp
    catalog.hpp
    visibility.hpp
    engine.hpp
    pose_timeline.hpp
  src/
    geo.cpp
    camera.cpp
    terrain.cpp
    catalog.cpp
    visibility.cpp
    engine.cpp
    pose_timeline.cpp
  adapters/desktop/
    dataset_loader.cpp
    geotiff_loader.cpp
    scene_loader.cpp
    session_loader.cpp
    result_writer.cpp
    svg_writer.cpp
  apps/
    tatra/main.cpp
    prepare/main.cpp
  tools/evaluation/
    reprojection_metrics.cpp
  tests/
    unit/
    integration/
    fixtures/
  benchmarks/
  examples/
  docs/
  data/                 # downloaded large data outside the repository
~~~

The application owns `Dataset`. `Dataset` owns the catalog and `GridTerrain`.
`PeakEngine` stores const references to them, so it cannot outlive `Dataset`. The
data does not change during calculation. `FrameResult` owns its result vector and
peak identifiers; it contains no pointers to temporary objects.

For this MVP, a plain catalog vector and linear filtering are a sufficient
starting point for tens or hundreds of peaks. Add a spatial index only when
measurements show a need.

Store geometry values as `double`. ECEF coordinates are on the order of millions
of metres; `float` may lose significant precision when subtracting nearby large
values. `float` saves memory in the raster, while interpolation is performed in
`double`.

`vector` owns data, while `span` and references only borrow it: they do not extend
its lifetime. This synchronous flow does not need shared ownership through
`shared_ptr`. A virtual `ITerrain` interface is justified because the height
source is replaceable; ordinary mathematical functions do not need to become a
class hierarchy.

**Conventions are part of the API and must be tested.**

| Concept | Convention |
|---|---|
| Geography | WGS84, latitude then longitude; field names explicitly include `deg` |
| Geometry height | Metres above the WGS84 ellipsoid |
| ECEF | Global WGS84 Cartesian frame, metres |
| ENU | x: east, y: north, z: up; origin at the observer |
| Camera | x: right, y: down, z: in front of the camera |
| Image | u right, v down; top-left image boundary: (0, 0); pixel centers: (i+0.5, j+0.5) |
| Heading | Degrees clockwise from true north |
| Pitch | Positive when the camera looks up |
| Roll | Positive rotates the “camera right” axis toward the “camera down” axis while keeping the viewing direction fixed |
| Quaternion | `camera_from_enu`: multiplies an ENU vector to produce a camera vector |
| Time | Int64 nanoseconds from the start of the session, with a shared time base |

Heading read from a file is relative to true north. A future phone adapter will
be responsible for magnetic-north correction and device-axis mapping. The library
must not guess what the input data means.

For heading=pitch=roll=0, the viewing direction is north:

~~~text
camera_x = ENU_east
camera_y = -ENU_up
camera_z = ENU_north
~~~

When constructing a rotation from heading ψ and pitch θ, build the basis with
angles in radians:

~~~text
forward = (sin ψ cos θ, cos ψ cos θ, sin θ)
right   = (cos ψ, -sin ψ, 0)
down    = forward × right

right_rolled = cos ρ * right + sin ρ * down
down_rolled  = -sin ρ * right + cos ρ * down

R_camera_from_enu has rows:
right_rolled^T, down_rolled^T, forward^T
~~~

This removes ambiguity about Euler-matrix multiplication order. Store a
quaternion in engine state and treat heading/pitch/roll as a convenient scenario
input.

**The declarations below define the contract; they are not yet the implementation.**

C++20 has no `std::expected`. `std::variant<T, Error>` is sufficient for this
project. `std::optional` means a value is genuinely absent, such as a missing
catalog elevation, rather than an error whose cause must be known.

~~~cpp
// types.hpp
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace tatra {

using Timestamp = std::chrono::nanoseconds;
using PeakId = std::string;  // e.g. "osm:node:123"; synthetic: "test:north"

enum class ErrorCode {
    InvalidInput, UnsupportedFormat, IoFailure, CorruptData,
    TimeOutsideSession, TimeGapTooLarge
};

struct Error {
    ErrorCode code;
    std::string message;
};

template<class T>
using Result = std::variant<T, Error>;

struct GeoCoord {
    double latitude_deg;
    double longitude_deg;
};

struct GeoPosition {
    GeoCoord coord;
    double ellipsoid_height_m;
};

// Separate types prevent accidentally passing ENU as ECEF.
struct EcefPoint { Eigen::Vector3d value_m; };
struct EnuPoint  { Eigen::Vector3d value_m; };

struct PixelPoint { double u; double v; };

struct CameraIntrinsics {
    int width_px;
    int height_px;
    double fx_px;
    double fy_px;
    double cx_px;
    double cy_px;
};

struct CameraPose {
    Eigen::Quaterniond camera_from_enu;
};

struct CatalogElevation {
    double metres;
    std::string vertical_reference; // may be "unspecified"; do not use it for geometry then
    std::string source;
};

enum class GeometryHeightSource { DemSample, VerifiedEllipsoid };

struct Peak {
    PeakId id;
    std::string name;
    GeoPosition geometry_position;
    GeometryHeightSource geometry_height_source;
    std::optional<CatalogElevation> display_elevation;
};

struct Observer {
    GeoPosition position;
};

} // namespace tatra
~~~

Heights are deliberately separated. The OSM catalog may contain `ele`, but it
does not always identify the vertical reference reliably. In the first package,
a peak's geometry position receives a height from the normalized DEM. The
displayed elevation comes from the catalog and records a separate provenance.
This is an approximation from a 30 m model: do not promise the exact position of
a small rock pinnacle.

For convenience, scene input may specify the observer as a height above the DEM,
for example 2 m. The adapter converts it to `GeoPosition`. This is a virtual
observer above the model surface, not an exact measurement of a person's
position, especially in a DSM containing trees.

Validate coordinate ranges, finite numbers, positive focal lengths, positive
image dimensions, and the quaternion. Reject a zero quaternion; a small deviation
from unit norm may be normalized according to an explicit policy. Do not replace
a rotation matrix with an arbitrary matrix without validation.

~~~cpp
// geo.hpp
namespace tatra {

EcefPoint toEcef(const GeoPosition&);
GeoPosition fromEcef(const EcefPoint&);

class LocalFrame {
public:
    explicit LocalFrame(const GeoPosition& origin);
    EnuPoint toEnu(const GeoPosition&) const;
    GeoPosition fromEnu(const EnuPoint&) const;
    // Internally: a local GeographicLib frame.
};

struct BearingElevation {
    double azimuth_deg;     // [0, 360)
    double elevation_deg;   // [-90, 90]
    double slant_range_m;   // vector length, not trail distance
};

Result<BearingElevation> bearingElevation(const EnuPoint&);

// camera.hpp
Result<CameraIntrinsics> cameraFromHorizontalFov(
    int width_px, int height_px, double horizontal_fov_deg);

Result<CameraPose> poseFromHeadingPitchRoll(
    double heading_deg, double pitch_deg, double roll_deg);

enum class ProjectionState { InFrame, OutsideFrame, BehindNearPlane };

struct Projection {
    ProjectionState state;
    std::optional<PixelPoint> pixel; // value for z > near_z, including outside the image
    double camera_depth_m;
};

Projection project(
    const EnuPoint& point,
    const CameraPose& pose,
    const CameraIntrinsics& camera,
    double near_z_m);

} // namespace tatra
~~~

`project` accepts already validated parameters. It calculates
`p_camera = q * p_enu`. For `z <= near_z`, it returns `BehindNearPlane` without
dividing by z. Otherwise:

~~~text
u = fx * x / z + cx
v = fy * y / z + cy
~~~

`InFrame` means `0 <= u < width` and `0 <= v < height`.
`cameraFromHorizontalFov` assumes square pixels:
`fx=fy=width/(2*tan(hfov/2))`, `cx=width/2`, `cy=height/2`. Complete intrinsics
may be read directly from the scene.

This is a pinhole model for an undistorted or already rectified image. Original
wide-angle photos may require rectification. Camera parameters must describe the
image after rotation, scaling, and cropping; simply copying EXIF data is not enough.

When importing calibration from a convention in which the center of the first
pixel is (0,0), convert the principal point and annotations to the boundary-based
convention used here by adding 0.5 to both coordinates. Do not mix the two
conventions in one report.

**Terrain is the only polymorphic interface needed initially.**

~~~cpp
// terrain.hpp
namespace tatra {

enum class TerrainSampleError { OutsideCoverage, NoData };

struct HeightSample {
    double ellipsoid_height_m;
};

using HeightResult = std::variant<HeightSample, TerrainSampleError>;

class ITerrain {
public:
    virtual ~ITerrain() = default;
    virtual HeightResult sampleHeight(GeoCoord) const = 0;
};

struct GridGeometry {
    std::size_t rows;
    std::size_t columns;
    double first_center_latitude_deg;
    double first_center_longitude_deg;
    double latitude_step_deg;   // negative for a north-up raster
    double longitude_step_deg;  // positive
};

class GridTerrain final : public ITerrain {
public:
    static Result<GridTerrain> create(
        GridGeometry,
        std::vector<float> heights_ellipsoid_m,
        std::vector<std::uint8_t> valid_mask);

    HeightResult sampleHeight(GeoCoord) const override;
    // Internally: geometry, heights, and mask.
};

} // namespace tatra
~~~

`GridTerrain` owns row-major data: `index=row*columns+column`. `sampleHeight`
performs bilinear interpolation. Outside the coverage it returns
`OutsideCoverage`; a missing required sample produces `NoData`. Zero is a valid
height, never a sentinel for missing data.

In this version, the adapter accepts only a north-up raster with WGS84 horizontal
coordinates and explicitly confirmed ellipsoidal heights. Other data is
normalized by `tatra-prepare` or rejected by the loader. An adapter must never
merely relabel the vertical reference without converting the values.

GDAL defines its geotransform relative to pixel boundaries; the center of the
first pixel is at (0.5, 0.5). Derive `first_center` from the geotransform returned
by GDAL. Do not add a second manual correction based on `RasterPixelIsPoint`.
[GDAL geotransform](https://gdal.org/en/stable/tutorials/geotransforms_tut.html)

One in-memory regional grid is enough for the MVP. Future tiling or file mapping
can implement `ITerrain` without changing the geometry or engine.

**Visibility is an assessment within the terrain model, with explicit sampling limits.**

~~~cpp
// visibility.hpp
namespace tatra {

enum class VisibilityState {
    VisibleInModel,
    OccludedInModel,
    Uncertain,
    MissingTerrain
};

struct VisibilityOptions {
    double sample_step_m = 10.0;
    double clearance_band_m = 0.5;
    double start_guard_m = 2.0;
    double target_guard_m = 30.0;
    std::size_t max_samples = 10000;
};

struct VisibilityResult {
    VisibilityState state;
    std::optional<double> min_sampled_clearance_m;
    std::optional<double> first_blocker_distance_m;
    std::size_t sample_count;
    std::string reason;
};

VisibilityResult traceVisibility(
    const Observer& observer,
    const Peak& peak,
    const ITerrain& terrain,
    const VisibilityOptions& options);

} // namespace tatra
~~~

The initial values are experimental parameters. The 0.5 m band does not describe
the accuracy of the real DEM. `VisibleInModel` does not guarantee that a peak is
visible in a photograph.

Reference algorithm:

1. Convert observer O and peak P to ECEF; calculate D=|P-O|.
2. Check endpoint consistency with the terrain. An observer below the model
   surface, or a peak clearly below the local DEM, produces `Uncertain`.
3. Sample the ECEF segment `X(t)=O+t*(P-O)` at intervals no greater than
   `sample_step_m`, after removing explicit guard sections at both ends. Divide
   the entire checked segment into `N=ceil(length/step)` parts and use their midpoints.
4. Convert each X to a geographic position. Sample terrain at its lat/lon.
5. Calculate `clearance = ellipsoidal height of X - ellipsoidal terrain height`.
6. A value below `-clearance_band_m` is a known occlusion. Record the first
   detected obstacle distance and stop if desired.
7. If no known obstacle exists but terrain is missing, return `MissingTerrain`.
   The full path was not checked.
8. If samples fall within the tolerance band or the checked segment is too short,
   return `Uncertain`. Only the remaining case is `VisibleInModel`.

`min_sampled_clearance` applies only to visited samples, including when evaluation
ends early. Do not call it the strict minimum of the entire line. Exceeding
`max_samples` produces `Uncertain` with a reason, rather than silently increasing
the step and returning a definitive result.

The target guard protects against the peak's own raster cell, but may omit an
obstacle in that region. Choose it for the package resolution, investigate the
effect of changing it, and record it in the result. For short segments dominated
by the excluded sections, return `Uncertain`. The initial 0.5–30 km candidate
range limits some such cases.

Sampling every 10 m does not prove that no narrow ridge was missed. Test
convergence at 10/5/2.5 m. More exact grid/mesh intersection is a separate future
task. The ECEF ray accounts for the Earth's geometric curvature; atmospheric
refraction remains outside the MVP.

**The engine composes completed functions rather than reimplementing them.**

~~~cpp
// catalog.hpp / engine.hpp
namespace tatra {

struct PeakCatalog {
    std::vector<Peak> peaks;
};

struct QueryOptions {
    double min_range_m = 500.0;
    double max_range_m = 30000.0;
    double near_z_m = 0.01;
    VisibilityOptions visibility;
};

struct FrameQuery {
    Timestamp timestamp;
    Observer observer;
    CameraPose pose;
    CameraIntrinsics camera;
    QueryOptions options;
};

struct PeakObservation {
    PeakId id;
    BearingElevation direction;
    Projection projection;
    // Empty for a peak on which the expensive test was not performed.
    std::optional<VisibilityResult> visibility;
};

struct FrameDiagnostics {
    std::size_t catalog_count;
    std::size_t outside_range_count;
    std::size_t projected_count;
    std::size_t visibility_sample_count;
};

struct FrameResult {
    Timestamp timestamp;
    std::vector<PeakObservation> observations;
    FrameDiagnostics diagnostics;
};

class PeakEngine {
public:
    PeakEngine(const ITerrain& terrain, const PeakCatalog& catalog);
    Result<FrameResult> evaluate(const FrameQuery& query) const;
    // Internally: const ITerrain& and const PeakCatalog&.
};

} // namespace tatra
~~~

`evaluate`:

1. Validates `FrameQuery`. An invalid camera or non-finite number is an `Error`
   for the entire query.
2. Creates the observer's `LocalFrame`.
3. Calculates ENU and range for every peak; rejects peaks outside the range.
4. Calculates azimuth, elevation, and `project`.
5. Calls `traceVisibility` for `InFrame`. Leaves `visibility` empty for all others.
6. Returns every in-range observation in stable ID order, along with diagnostics.

Missing terrain along one line of sight is a result for that specific peak, not
a reason to lose all other results. The application may warn about partial
coverage and still display the remaining data.

The core does not decide whether to render an uncertain label. By default, SVG
shows `VisibleInModel` in the normal color, distinguishes
`Uncertain`/`MissingTerrain` with a different style, and hides
`OccludedInModel`. Debug mode shows every status. Always export the complete
assessment to JSON.

Do not log to stdout from geometry code. Do not measure execution time inside
`evaluate`. Timing belongs in the benchmark/CLI and a separate file; this keeps
the result deterministic.

**Replay also verifies time and orientation.**

~~~cpp
// pose_timeline.hpp
namespace tatra {

struct PoseSample {
    Timestamp timestamp;
    CameraPose pose;
};

class PoseTimeline {
public:
    static Result<PoseTimeline> create(std::vector<PoseSample> samples);
    Result<CameraPose> sampleAt(
        Timestamp time,
        Timestamp max_gap) const;
};

struct ReplayFrame {
    Timestamp timestamp;
    std::optional<std::string> image_path;
};

struct Session {
    Observer observer;                 // fixed in this version
    CameraIntrinsics camera;           // fixed in this version
    std::vector<PoseSample> poses;
    std::vector<ReplayFrame> frames;
};

} // namespace tatra
~~~

`create` rejects non-finite/zero rotations and negative, non-monotonic, or
duplicate timestamps. `sampleAt` returns the sample for an exact timestamp,
performs shortest-path SLERP between samples, and returns an error outside the
range. An excessive gap produces `TimeGapTooLarge`. Do not extrapolate silently.

Replay iterates over the supplied frame times, retrieves `poseAt`, and calls the
same `evaluate`. It does not wait for real time or use `sleep`. An invalid frame
receives a recorded error; the report includes the number of skipped frames.

The observer position is fixed in this version. Interpolation of a moving
position and raw accelerometer/gyroscope data belong to a later extension. Scene
timestamps are perfectly synchronized; a future phone adapter must separately
establish clock alignment between the camera and IMU.

**Adapters have small, explicit contracts.**

~~~cpp
// Conceptual desktop-adapter declarations; dataset/scene types
// contain the metadata described below.
Result<Dataset> loadDataset(const std::filesystem::path&);
Result<FrameQuery> loadScene(
    const std::filesystem::path&, const ITerrain&);
Result<Session> loadSession(
    const std::filesystem::path&, const ITerrain&);

// std::monostate represents success without a value in Result.
Result<std::monostate> writeFrameJson(
    const FrameResult&, const PeakCatalog&,
    const RunMetadata&, const std::filesystem::path&);

Result<std::monostate> writeOverlaySvg(
    const FrameResult&, const PeakCatalog&,
    const RenderOptions&, const std::filesystem::path&);
~~~

JSON/SVG output records the package ID and hash, program version, and scene
options. SVG must correctly escape XML names, including `&`, `<`, and `>`. The
marker position represents the peak point; text may be offset and connected with
a line. Error tests apply to the marker, never the text position.

A simple deterministic label layout is sufficient initially: a fixed priority
order, several candidate text positions, and rejection of colliding rectangles.
When no room remains, hide the text but retain the marker and complete JSON record.

**The data package must be reproducible.**

~~~text
data/tatras-v1/
  manifest.json
  terrain-ellipsoid.tif
  peaks.json
  ATTRIBUTION.txt

examples/session/
  session.json
  poses.json
  frames.json
  images/                 # optional
  annotations.json        # optional
~~~

`manifest.json` records: `schema_version`, `dataset_id`, download date, original
URLs and file identifiers, SHA-256 values, horizontal reference system, original
and output vertical reference systems, geoid model used, raster dimensions and
step, NoData, coverage area, resampling method, tool version, and required source
attributions.

A scene may be described as follows:

~~~json
{
  "schema_version": 1,
  "timestamp_ns": 0,
  "observer": {
    "latitude_deg": 49.30,
    "longitude_deg": 19.98,
    "height": { "mode": "above_dem", "metres": 2.0 },
    "simulated": true
  },
  "camera": {
    "width_px": 1280,
    "height_px": 720,
    "horizontal_fov_deg": 60.0
  },
  "orientation": {
    "heading_deg": 180.0,
    "pitch_deg": 5.0,
    "roll_deg": 0.0
  },
  "options": {
    "min_range_m": 500.0,
    "max_range_m": 30000.0
  }
}
~~~

This is an illustrative virtual scene, not a verified photographic viewpoint,
which is explicitly recorded as `observer.simulated=true`. For a real photo,
record actual or explicitly estimated coordinates, intrinsics, and orientation.
An `ellipsoid` value supplied directly is the alternative to `above_dem`; do not
allow both at once.

**Prepare the Tatras data in the following order.**

1. Download GLO-30 DGED/GeoTIFF from [Copernicus Data Space](https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM).
   GLO-30 access is available to registered users. For the working DEM area
   west=19.4, south=49.0, east=20.6, north=49.6, select tiles intersecting that
   rectangle. This is a buffered test area, not an official Tatras boundary.
2. Preserve the raw files and their identifiers. Merge and crop them with GDAL;
   verify coverage, NoData, and edge consistency. Do not artificially upscale a
   30 m resolution to 1 m.
3. Convert EGM2008 heights to WGS84 ellipsoidal heights. Download the
   `egm2008-2_5` model from the [GeographicLib data](https://geographiclib.sourceforge.io/C++/doc/geoid.html).
   For every valid pixel, calculate `h=H+N(latitude,longitude)`, where N comes
   from the geoid model. Record the version/model and hash. The smaller resulting
   regional package does not require the geoid while the engine runs.
4. Download `natural=peak` nodes through Overpass; save the raw JSON instead of
   querying the API on every run. Alternatively, use the [Poland](https://download.geofabrik.de/europe/poland.html)
   and [Slovakia](https://download.geofabrik.de/europe/slovakia.html) PBF extracts,
   clipping the region and deduplicating by OSM identifier.
5. Select 30–100 peaks for the first catalog. Preserve ID, lat/lon, `name`,
   `name:pl`, `name:sk`, `ele`, and source. Do not merge records merely because
   they share a name.
6. Sample each node's geometry height from the prepared DEM. Missing coverage
   means rejecting the record with a report. A missing `ele` does not remove a
   peak; the UI then shows no verified display elevation.
7. Write the package and validation report. Re-preparing the same source files
   should produce identical data content except for explicitly variable process
   metadata.

GLO-30 is a DSM: a surface model that also includes vegetation and buildings.
Its grid is angular, so “30 m” does not mean cells of exactly 30×30 m. Its
vertical reference is EGM2008. Do not treat it as an error-free ground model or
an exact catalog of summit heights.
[Copernicus DEM specification](https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM)

Example [Overpass Turbo](https://overpass-turbo.eu/) query:

~~~text
[out:json][timeout:60];
node["natural"="peak"](49.10,19.55,49.35,20.40);
out body;
~~~

Overpass bounding-box order is south, west, north, east. This catalog rectangle
is smaller than the DEM area. The MVP imports nodes only; document this
completeness limit. Details: [natural=peak](https://wiki.openstreetmap.org/wiki/Tag:natural%3Dpeak),
[Overpass QL](https://wiki.openstreetmap.org/wiki/Overpass_API/Overpass_QL).

Separate these functions in `tatra-prepare`:

~~~cpp
readRawDem(...);
normalizeHeightGridToEllipsoid(...);
parseOsmPeakNodes(...);
validateAndDeduplicatePeaks(...);
assignGeometryHeights(...);
writeDatasetManifest(...);
~~~

These functions belong in the data tool, not `PeakEngine`. The code license and
data terms are separate concerns. Preserve Copernicus and
[OpenStreetMap](https://www.openstreetmap.org/copyright) attribution in the
package and demo documentation.

After the MVP, the Polish [DTM](https://www.geoportal.gov.pl/pl/dane/numeryczny-model-terenu-nmt/)
and Slovak [DMR](https://www.skgeodesy.sk/gku/produkty-sluzby/na-stiahnutie/zbgis.html?forceBrowserDetector=blind)
may be evaluated. Combining national vertical reference systems is a separate
task, not ordinary raster cropping.

**Tests use different sources of truth.**

| Layer | Example | What it proves |
|---|---|---|
| Analytical | Known ECEF points, manually calculated projection | Formula and convention correctness |
| Synthetic terrain | Flat surface, high ridge, missing data | Occlusion and error behavior |
| Adapters | Small GeoTIFF and JSON with known content | Correct metadata and reading |
| Integration | Scene with several synthetic peaks | Module cooperation |
| Replay | Rotation with a gap between samples | Interpolation, time, and repeatability |
| Photos | Manually annotated summits from a known location | Model-to-image agreement |
| Performance | Fixed dataset, Release, described machine | Computation cost and whether optimization is worthwhile |

Basic independent check values:

- WGS84: lat=0, lon=0, h=0 gives ECEF (6378137,0,0) m.
- With the ENU origin at the equator and lon=0: ECEF +Y means east, +Z north,
  and +X up.
- Pinhole fx=fy=1000, cx=640, cy=360: camera point (0,0,1000) gives (640,360),
  while (100,0,1000) gives (740,360).
- Camera points with `z<=near_z` must not cause division by zero.
- A 2×2 grid with heights 100,120 / 140,160 gives 130 at the center.
- The midpoint between orientation samples 359° and 1° is near 0°, not 180°.

A geodetic round-trip test is necessary but not sufficient: two incorrect,
mutually inverse transformations may pass it.

Use photos from your own archives, friends, or Wikimedia Commons, preserving the
author, file URL, and terms of use. The statement “the photo shows Giewont” does
not prove the camera location is known. Coordinates on a page may identify the
photographed object rather than the photographer.
[Commons file reuse guidelines](https://commons.wikimedia.org/wiki/Commons:Contact_us/Reuse)

For every reference scene, record:

- coordinates and how the observer position was established;
- the source of the observer height;
- original dimensions, rotation, crop, and any image rectification;
- camera parameters and which are known versus estimated;
- manually annotated points `(peak_id,u,v)` and annotation uncertainty;
- which points were used for alignment and which were reserved for evaluation.

Start with 3 photos from 3 viewpoints, each with at least 5 annotated peaks. Use
selected 1–2 points for manual heading/pitch correction and the rest for
measurement. With such a small set, the report describes those scenes rather
than general accuracy across the Tatras.

A cylindrical/spherical stitched panorama requires a different projection model
or extraction of a correct perspective view. Do not pass it directly to the
pinhole camera model.

**Metrics must also report omitted cases.**

~~~cpp
struct PeakAnnotation {
    PeakId id;
    PixelPoint pixel;
};

struct ReprojectionMetrics {
    std::size_t annotated_count;
    std::size_t matched_count;
    std::size_t missing_count;
    std::optional<double> median_error_px;
    std::optional<double> p95_error_px;
    std::optional<double> median_error_deg;
    std::optional<double> p95_error_deg;
};

ReprojectionMetrics measureReprojection(
    std::span<const PeakAnnotation>,
    std::span<const PeakObservation>,
    const CameraIntrinsics&);
~~~

Pixel error is the Euclidean distance between the marker and annotation with the
same ID. Calculate angular error between normalized rays `K^-1[u,v,1]`, clamping
the dot product to [-1,1] before `acos`. Do not simply divide pixels by one fixed
conversion factor across a wide image.

No matched points means no metrics, not zero error. Define the percentile method,
for example nearest-rank for p95. For a small number of points, also show every
individual error.

Evaluate visibility separately on synthetic scenes and on manually described,
unambiguous cases. A peak missing from a cloudy photo does not prove terrain
occlusion.

Measure the number of visibility samples and p50/p95 timings separately for data
preparation, `evaluate`, and SVG writing. As an initial engineering target, use
`evaluate` below 1 s p95 for 100 candidates, a range up to 30 km, and a fixed
step on your machine in Release. This is a target to measure, not a claim of
achieved performance or a phone real-time requirement.

**The MVP is complete when all of the following conditions are met.**

- The core and synthetic tests build without a mobile SDK and without GDAL.
- A synthetic scene has exactly the expected directions, projections, and
  visibility statuses.
- The PL/SK package documents vertical reference systems, sources, and coverage.
- At least 30 selected peaks have stable IDs; display elevations are verified or
  explicitly unknown.
- The CLI renders a scene and an orientation series to SVG/JSON.
- Missing terrain and invalid input do not produce deceptively valid labels.
- Replay is repeatable within the same build; cross-platform comparisons use
  explicit numeric tolerances.
- There are 3 documented photographic scenes and an error report for points not
  used in alignment.
- Tests pass in Debug/Release; ASan/UBSan find no issues in the test set.
- The repository contains demo reproduction instructions, limitations, and
  measurements from named hardware.

Proposed threshold for the final photographic demonstration: at least 80% of the
annotated points reserved for evaluation have a prediction in the image, with a
median angular error <=1°. This is a prototype threshold after manual alignment,
not a promise of live operation. Set it before evaluation and also show p95 and
failed cases. If the camera/position provenance is too uncertain, the scene
remains a demonstration and does not satisfy this criterion.

First complete the input → geometry → result flow using synthetic data. Then add
the terrain model, real data, replay, and photo evaluation. Implement one item at
a time from the [task list](MVP_TASKS.md), recording the test result and a short
technical conclusion for each.
