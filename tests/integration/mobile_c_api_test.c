#include <tatra/mobile.h>

#include <stdint.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            return __LINE__;                                                                       \
    } while (0)

int main(void) {
    float heights[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    uint8_t mask[4] = {255, 255, 255, 255};
    char peak_id[] = "visible";
    const tatra_mobile_peak peak = {peak_id, "Visible", 0.01, 0.0, 1000.0, 1, 999.0};
    const tatra_mobile_create_info create_info = {
        {2, 2, 0.1, -0.1, -0.2, 0.2, heights, mask, 4}, &peak, 1};
    char error[256];
    tatra_mobile_engine* engine = NULL;
    CHECK(tatra_mobile_abi_version() == TATRA_MOBILE_ABI_VERSION);
    CHECK(strlen(tatra_mobile_version()) > 0);
    CHECK(tatra_mobile_engine_create(&create_info, &engine, error, sizeof(error)) ==
          TATRA_MOBILE_OK);
    CHECK(engine != NULL);
    CHECK(tatra_mobile_engine_peak_count(engine) == 1);

    /* The engine owns copies, so caller memory may change immediately after create. */
    heights[0] = heights[1] = heights[2] = heights[3] = 2000.0F;
    mask[0] = mask[1] = mask[2] = mask[3] = 0;
    peak_id[0] = 'X';

    tatra_mobile_query query = {0};
    query.timestamp_ns = 123456;
    query.observer_latitude_deg = 0.0;
    query.observer_longitude_deg = 0.0;
    query.observer_ellipsoid_height_m = 1000.0;
    query.observer_simulated = 1;
    query.camera_from_enu_w = 0.7071067811865476;
    query.camera_from_enu_x = 0.7071067811865476;
    query.image_width_px = 1280;
    query.image_height_px = 720;
    query.fx_px = query.fy_px = 1000.0;
    query.cx_px = 640.0;
    query.cy_px = 360.0;
    query.min_range_m = 20.0;
    query.max_range_m = 5000.0;
    query.near_z_m = 0.01;
    query.visibility_sample_step_m = 100.0;
    query.visibility_clearance_band_m = 0.5;
    query.visibility_start_guard_m = 2.0;
    query.visibility_target_guard_m = 30.0;
    query.visibility_max_samples = 100;

    size_t count = 0;
    tatra_mobile_diagnostics diagnostics;
    CHECK(tatra_mobile_engine_evaluate(engine, &query, NULL, 0, &count, &diagnostics, error,
                                       sizeof(error)) == TATRA_MOBILE_BUFFER_TOO_SMALL);
    CHECK(count == 1);
    CHECK(strstr(error, "buffer") != NULL);

    tatra_mobile_observation observation;
    CHECK(tatra_mobile_engine_evaluate(engine, &query, &observation, 1, &count, &diagnostics, error,
                                       sizeof(error)) == TATRA_MOBILE_OK);
    CHECK(count == 1);
    CHECK(strcmp(observation.peak_id, "visible") == 0);
    CHECK(strcmp(observation.peak_name, "Visible") == 0);
    CHECK(observation.has_display_elevation == 1);
    CHECK(observation.display_elevation_m == 999.0);
    CHECK(observation.projection_state == TATRA_MOBILE_IN_FRAME);
    CHECK(observation.has_pixel == 1);
    CHECK(observation.pixel_u > 639.999 && observation.pixel_u < 640.001);
    CHECK(observation.visibility_state == TATRA_MOBILE_VISIBLE_IN_MODEL);
    CHECK(diagnostics.catalog_count == 1);
    CHECK(diagnostics.projected_count == 1);

    query.camera_from_enu_w = 0.0;
    query.camera_from_enu_x = 0.0;
    CHECK(tatra_mobile_engine_evaluate(engine, &query, &observation, 1, &count, &diagnostics, error,
                                       sizeof(error)) == TATRA_MOBILE_INVALID_ARGUMENT);
    CHECK(strstr(error, "quaternion") != NULL);

    tatra_mobile_engine_destroy(engine);
    tatra_mobile_engine_destroy(NULL);
    return 0;
}
