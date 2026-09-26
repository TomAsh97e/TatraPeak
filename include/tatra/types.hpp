#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <chrono>
#include <optional>
#include <string>
#include <variant>

namespace tatra {

using Timestamp = std::chrono::nanoseconds;
using PeakId = std::string;

enum class ErrorCode {
    InvalidInput,
    UnsupportedFormat,
    IoFailure,
    CorruptData,
    TimeOutsideSession,
    TimeGapTooLarge,
    NotImplemented
};

struct Error {
    ErrorCode code;
    std::string message;
};

template <class T> using Result = std::variant<T, Error>;
using Status = Result<std::monostate>;

struct GeoCoord {
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
};

struct GeoPosition {
    GeoCoord coord;
    // Height in metres above the WGS84 reference ellipsoid.
    double ellipsoid_height_m = 0.0;
};

struct EcefPoint {
    Eigen::Vector3d value_m = Eigen::Vector3d::Zero();
};
struct EnuPoint {
    Eigen::Vector3d value_m = Eigen::Vector3d::Zero();
};
struct PixelPoint {
    double u = 0.0;
    double v = 0.0;
};

struct CameraIntrinsics {
    int width_px = 0;
    int height_px = 0;
    double fx_px = 0.0;
    double fy_px = 0.0;
    double cx_px = 0.0;
    double cy_px = 0.0;
};

struct CameraPose {
    // Maps ENU vectors into camera coordinates (+x right, +y down, +z forward).
    // Identity is a mathematical identity, NOT the north-facing camera pose.
    Eigen::Quaterniond camera_from_enu = Eigen::Quaterniond::Identity();
};

struct CatalogElevation {
    double metres = 0.0;
    std::string vertical_reference;
    std::string source;
};

enum class GeometryHeightSource { DemSample, VerifiedEllipsoid };

struct Peak {
    PeakId id;
    std::string name;
    GeoPosition geometry_position;
    GeometryHeightSource geometry_height_source = GeometryHeightSource::DemSample;
    std::optional<CatalogElevation> display_elevation;
};

struct Observer {
    GeoPosition position;
    // True for a virtual viewpoint that is not a surveyed camera position.
    bool simulated = false;
};

} // namespace tatra
