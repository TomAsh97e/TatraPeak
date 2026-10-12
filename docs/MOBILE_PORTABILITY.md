# Portable Android/iOS Core

TatraPeak provides two integration layers with no dependency on a phone SDK:

- `TatraPeak::core` — the public C++20 API;
- `TatraPeak::mobile` and [mobile.h](../include/tatra/mobile.h) — a stable C ABI
  boundary for Objective-C/Swift and a thin JNI layer on Android.

Both targets use only Eigen and a statically linked GeographicLib. They do not
depend on GDAL, nlohmann/json, the file system, network, camera, GUI, or the
Android/iOS SDK. GDAL and the data preparation tools remain on the desktop. The
phone receives a prepared regional Float32 raster, a mask, and a peak catalog.

```text
desktop: DEM + OSM -> tatra-prepare -> application assets
                                      |
phone: assets -> platform decoder -> C ABI -> results -> native overlay
                GPS/IMU/camera --------^                 |
                axis/north correction -------------------+
```

## C ABI Contract

`tatra_mobile_engine_create` copies the raster, mask, records, and strings. Once
it returns, the application may release the input buffers.
`tatra_mobile_engine_destroy` releases everything. Creating a large raster
temporarily requires memory for both the input and its copy; the source buffers
should be released immediately after a successful call.

Before each frame, the application fills `tatra_mobile_query` and then calls
`tatra_mobile_engine_evaluate`. `tatra_mobile_engine_peak_count` always provides
a sufficient result-buffer size. If the buffer is too small, the function writes
nothing partially, returns `TATRA_MOBILE_BUFFER_TOO_SMALL`, and reports the
required size.

Result structures belong to the caller. The `peak_id` and `peak_name` fields
point to engine-owned strings that remain valid until the engine is destroyed;
copy them if they must live longer. A separate optional display-elevation field
prevents the ellipsoidal height used by the geometry from being shown as an
elevation above sea level. A single engine instance is not thread-safe. Separate
instances may run concurrently, or access may be synchronized externally. No
C++ exception crosses the C ABI — a status and an optional message buffer
describe the error.

`TATRA_MOBILE_ABI_VERSION` and `tatra_mobile_abi_version()` allow wrappers to
check compatibility. Public state types have an explicit `int32_t` size; counts
use native `size_t`. The integration test is written in C, not C++:
[mobile_c_api_test.c](../tests/integration/mobile_c_api_test.c).

## Shared Build Without the Desktop Layer

The same variant is verified on a desktop with this preset:

```sh
cmake --preset mobile-core
cmake --build --preset mobile-core --parallel 2
ctest --preset mobile-core
```

The preset disables the CLI and `tatra_io`, but builds `tatra_core`,
`tatra_mobile`, and the synthetic tests. The first configuration may download
pinned dependency sources; subsequent builds and tests do not need network access.

## Android NDK

Example for arm64 devices (replace the path with your own NDK path):

```sh
cmake -S . -B build/android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/android-ndk/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release \
  -DTATRA_BUILD_TESTS=OFF -DTATRA_BUILD_CLI=OFF \
  -DTATRA_BUILD_DESKTOP_IO=OFF -DTATRA_BUILD_MOBILE_BRIDGE=ON
cmake --build build/android-arm64 --target tatra_mobile --parallel 2
```

In the application project, the simplest approach is to add the repository with
`add_subdirectory` and link `TatraPeak::mobile` into a small JNI library. Kotlin
does not call the C ABI directly — JNI is responsible only for converting
arrays/structures and managing the handle. Algorithms and validation remain in
the shared code.

## iOS

Static libraries for an arm64 device:

```sh
cmake -S . -B build/ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DTATRA_BUILD_TESTS=OFF -DTATRA_BUILD_CLI=OFF \
  -DTATRA_BUILD_DESKTOP_IO=OFF -DTATRA_BUILD_MOBILE_BRIDGE=ON
cmake --build build/ios --config Release --target tatra_mobile --parallel 2
```

The C header can be exposed to Swift through a bridging header or module map.
The target XCFramework should combine device and simulator variants; packaging
it belongs to the application project, not the computation core.

CI runs host tests on Linux, Windows, and macOS, cross-compiles `tatra_mobile`
for Android arm64-v8a and iOS arm64, and runs ASan/UBSan on Linux.

## Phone Data — Adapter Responsibilities

The platform layer must provide:

- a WGS84 position and **ellipsoidal** height, rather than blindly using a
  GPS/map elevation;
- a `camera_from_enu` quaternion in `w,x,y,z` order;
- heading relative to true north, including magnetic-declination correction;
- a mapping from device axes to camera axes and the current screen orientation;
- intrinsics appropriate for the lens, crop, stabilization, and frame size;
- a common time base for pose/orientation samples and frames;
- processed regional assets that match the data-format version.

The library deliberately does not guess these values. An incorrect sensor
convention can produce a numerically valid but visually shifted overlay. Start
by recording a short phone session and replaying it offline before adding live mode.

## What “Portable” Means in This Repository

The engine and its ABI boundary are portable: the same code, the same tests, no
mobile-system dependencies, and cross-build configurations. The [android](../android/README.md)
directory contains the Kotlin/JNI application, CameraX diagnostics, intrinsics,
sensor and location handling, and synthetic tests that also run on the host JVM.
Platform code remains outside the core. Acceptance status and limitations are
described in the [Android stage report](ANDROID_PROGRESS.md). The real package
uses the approved [TATRAPK v1 format](ANDROID_DATA_FORMAT_PROPOSAL.md), produced
by the desktop converter and verified by the Android loader before JNI. There is
not yet a Swift adapter or confirmed calibration/orientation on the Galaxy S10e.
