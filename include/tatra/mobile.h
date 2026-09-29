#ifndef TATRA_MOBILE_H
#define TATRA_MOBILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TATRA_MOBILE_ABI_VERSION 1

typedef struct tatra_mobile_engine tatra_mobile_engine;

typedef int32_t tatra_mobile_status;
enum {
    TATRA_MOBILE_OK = 0,
    TATRA_MOBILE_INVALID_ARGUMENT = 1,
    TATRA_MOBILE_BUFFER_TOO_SMALL = 2,
    TATRA_MOBILE_CORE_ERROR = 3,
    TATRA_MOBILE_INTERNAL_ERROR = 4
};

typedef int32_t tatra_mobile_projection_state;
enum {
    TATRA_MOBILE_IN_FRAME = 0,
    TATRA_MOBILE_OUTSIDE_FRAME = 1,
    TATRA_MOBILE_BEHIND_NEAR_PLANE = 2
};

typedef int32_t tatra_mobile_visibility_state;
enum {
    TATRA_MOBILE_VISIBILITY_NOT_EVALUATED = -1,
    TATRA_MOBILE_VISIBLE_IN_MODEL = 0,
    TATRA_MOBILE_OCCLUDED_IN_MODEL = 1,
    TATRA_MOBILE_UNCERTAIN = 2,
    TATRA_MOBILE_MISSING_TERRAIN = 3
};

typedef struct tatra_mobile_grid {
    size_t rows;
    size_t columns;
    double first_center_latitude_deg;
    double first_center_longitude_deg;
    double latitude_step_deg;
    double longitude_step_deg;
    const float* heights_ellipsoid_m;
    const uint8_t* valid_mask;
    size_t sample_count;
} tatra_mobile_grid;

typedef struct tatra_mobile_peak {
    const char* id;
    const char* name;
    double latitude_deg;
    double longitude_deg;
    double ellipsoid_height_m;
    uint8_t has_display_elevation;
    double display_elevation_m;
} tatra_mobile_peak;

typedef struct tatra_mobile_create_info {
    tatra_mobile_grid terrain;
    const tatra_mobile_peak* peaks;
    size_t peak_count;
} tatra_mobile_create_info;

typedef struct tatra_mobile_query {
    int64_t timestamp_ns;
    double observer_latitude_deg;
    double observer_longitude_deg;
    double observer_ellipsoid_height_m;
    uint8_t observer_simulated;

    /* Quaternion mapping ENU to camera coordinates, in explicit w,x,y,z order. */
    double camera_from_enu_w;
    double camera_from_enu_x;
    double camera_from_enu_y;
    double camera_from_enu_z;

    int32_t image_width_px;
    int32_t image_height_px;
    double fx_px;
    double fy_px;
    double cx_px;
    double cy_px;

    double min_range_m;
    double max_range_m;
    double near_z_m;
    double visibility_sample_step_m;
    double visibility_clearance_band_m;
    double visibility_start_guard_m;
    double visibility_target_guard_m;
    size_t visibility_max_samples;
} tatra_mobile_query;

typedef struct tatra_mobile_observation {
    /* Strings are owned by the engine and remain valid until engine destruction. */
    const char* peak_id;
    const char* peak_name;
    uint8_t has_display_elevation;
    double display_elevation_m;
    double azimuth_deg;
    double elevation_deg;
    double slant_range_m;
    tatra_mobile_projection_state projection_state;
    uint8_t has_pixel;
    double pixel_u;
    double pixel_v;
    double camera_depth_m;
    tatra_mobile_visibility_state visibility_state;
    uint8_t has_min_sampled_clearance;
    double min_sampled_clearance_m;
    uint8_t has_first_blocker_distance;
    double first_blocker_distance_m;
    size_t visibility_sample_count;
} tatra_mobile_observation;

typedef struct tatra_mobile_diagnostics {
    size_t catalog_count;
    size_t outside_range_count;
    size_t projected_count;
    size_t visibility_sample_count;
} tatra_mobile_diagnostics;

/* The create function copies the complete raster, mask, peak strings and peak records. */
tatra_mobile_status tatra_mobile_engine_create(const tatra_mobile_create_info* info,
                                               tatra_mobile_engine** out_engine,
                                               char* error_message, size_t error_message_capacity);

void tatra_mobile_engine_destroy(tatra_mobile_engine* engine);

size_t tatra_mobile_engine_peak_count(const tatra_mobile_engine* engine);

/*
 * The caller owns observations. Use peak_count as a sufficient capacity.
 * No partial observations are written when capacity is too small; out_count receives
 * the required count. The engine is intentionally not thread-safe.
 */
tatra_mobile_status
tatra_mobile_engine_evaluate(tatra_mobile_engine* engine, const tatra_mobile_query* query,
                             tatra_mobile_observation* observations, size_t observation_capacity,
                             size_t* out_count, tatra_mobile_diagnostics* diagnostics,
                             char* error_message, size_t error_message_capacity);

uint32_t tatra_mobile_abi_version(void);
const char* tatra_mobile_version(void);

#ifdef __cplusplus
}
#endif

#endif
