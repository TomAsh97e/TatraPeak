# Android MVP — Status as of 2026-10-06

No tests have been run on a phone, following the decision to continue without
connecting the Samsung Galaxy S10e. Hardware results below are marked as not run.

## Stage 1 — Build and JNI

Completed on the development machine:

- Gradle/Kotlin project, minSdk 26, `arm64-v8a` only, pinned NDK 27.2.12479018;
- `libtatra_jni.so` links `TatraPeak::mobile`; the CLI, desktop IO, CMake tests,
  and benchmarks are disabled in the APK build;
- `NativeEngine : Closeable`, create/evaluate/destroy, C ABI statuses, and an
  exception barrier;
- a real JNI smoke test corresponding to `mobile_c_api_test.c`, run on the host JVM;
- a diagnostics screen showing the synthetic result.

Files: `android/settings.gradle.kts`, `android/build.gradle.kts`,
`android/app/build.gradle.kts`, `android/app/src/main/cpp/`,
`android/app/src/main/java/pl/tatrapeak/engine/`, `SyntheticScene.kt`,
`NativeEngineTest.kt`, `MainActivity.kt`.

Phone: installation, launch, arm64 JNI test, and leak measurement — not run.

## Stage 2 — Camera

Completed on the development machine:

- CameraX `PreviewView` with `FIT_CENTER`, ImageAnalysis, and a transparent overlay;
- selection of a rear 1× candidate based on focal length and sensor size, without
  a fixed cameraId;
- requests for a physical camera ID, 1× zoom, and disabled EIS/OIS, while showing
  the actual response metadata;
- explicit `bufferToView = sensorToView * inverse(sensorToBuffer)`;
- tests for corners, center, crop, and all quarter-turn rotations.

Files: `camera/CameraController.kt`, `camera/ImageGeometry.kt`, `ui/OverlayView.kt`,
`ImageGeometryTest.kt`, the manifest, and CameraX dependencies.

Phone: primary-lens selection, overlay point alignment, and screen rotation — not run.

## Stage 3 — Intrinsics

Completed on the development machine:

- preferred `LENS_INTRINSIC_CALIBRATION`;
- explicit fallback based on focal length, sensor size, and pixel array;
- conversion of fx/fy/cx/cy through sensor→buffer, crop, and rotation;
- the source and approximations are visible in diagnostics; skew or inconsistent
  active arrays block use instead of producing a deceptively valid result.

Files: `camera/CameraIntrinsics.kt`, `camera/Intrinsics.kt`, `IntrinsicsTest.kt`.

Phone: metadata reliability, ISP crop, distortion, and calibration — not run.

## Stage 4 — Orientation

Completed on the development machine:

- `TYPE_ROTATION_VECTOR`, sample timestamps, and a frame-matching buffer (25 ms);
- explicit ENU→camera matrix, SENSOR_ORIENTATION, buffer rotation, and declination;
- a w,x,y,z quaternion and diagnostic heading/pitch/roll;
- independent tests for north, east, zenith, landscape, roll, and declination sign;
- a full convention description in `ANDROID_ORIENTATION.md`.

Files: `sensors/OrientationMath.kt`, `sensors/OrientationTracker.kt`,
`OrientationTest.kt`, `docs/ANDROID_ORIENTATION.md`.

Phone: axes, timestamps, and readings on the Galaxy S10e — not run.

## Stage 5 — Location

Completed on the development machine:

- CAMERA and FINE/COARSE permissions in the manifest, including denial handling;
- current GPS updates without unconditional use of lastLocation;
- ellipsoidal `Location.altitude`, hasAltitude, and accuracy values;
- rejection of samples older than 15 s, with XY error above 30 m, or Z error
  above 50 m;
- a manual test position preserved when the Activity is recreated;
- declination through `GeomagneticField`, with evaluate paused when data is missing;
- evaluate at most 8 times/s off the UI thread, with backpressure and publication
  of results to the UI.

Files: `location/LocationPolicy.kt`, `location/LocationTracker.kt`,
`LocationPolicyTest.kt`, `engine/FrameEvaluator.kt`, `MainActivity.kt`.

Phone: permissions, GPS, declination, and behavior without sensors — not run.

## Stage 6 — Offline Data

Completed on the development machine:

- approved TATRAPK v1 format and the `tatra-prepare mobile` command;
- the converter first loads and validates the existing `tatras-v1` package;
- before JNI, the Android loader checks length, version, overflow, reserved fields,
  SHA-256, UTF-8, mask, dimensions, records, and trailing bytes;
- the generated local asset is about 45 MiB, with a 2160×4321 raster and 50 peaks,
  SHA-256 `5e6ca2e3405d45ad0dc4781b2ac70228fdfda4baa9a33aff3d5a6eab5f5e4e1b`;
- the asset is stored uncompressed in the APK and ignored by Git; a missing asset
  causes an explicit fallback to the synthetic fixture.

Files: `adapters/desktop/mobile_package_writer.cpp`, `io.hpp`, `apps/prepare/main.cpp`,
`data/MobilePackage.kt`, `MobilePackageTest.kt`, `ANDROID_DATA_FORMAT_PROPOSAL.md`.

Phone: loader peak-memory usage and engine creation time — not run.

## Stage 7 — Working Overlay

Completed on the development machine:

- evaluate runs on a single worker with backpressure, at most 8 times/s;
- the result reaches the UI, and the overlay skips OutsideFrame/BehindNearPlane;
- the name, optional elevation, and five visibility states use distinct colors;
- CameraX, sensors, GPS, and the engine are closed with the lifecycle;
- a button records up to 10 s / 100 frames in the existing T16 replay format.

Files: `engine/FrameEvaluator.kt`, `ui/OverlayView.kt`, `ui/MainActivity.kt`,
`data/DiagnosticRecorder.kt`.

Phone: label readability, 5–10 Hz performance, lifecycle, and replay recording — not run.

## Development-Machine Verification

```text
cmake --preset mobile-core
cmake --build --preset mobile-core --parallel 2
ctest --preset mobile-core                         34/34 passed
cd android
./gradlew test                                     26/26 Debug + 26/26 Release passed
./gradlew assembleDebug                            passed
./gradlew lintDebug                                passed
./gradlew assembleDebugAndroidTest                 passed
```

Application APK: `android/app/build/outputs/apk/debug/app-debug.apk` (arm64-v8a).
Test APK: `android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk`.
`libtatra_jni.so` depends only on Android system libraries (`libm`, `libdl`,
`libc`); GDAL and nlohmann/json did not enter the APK.

`connectedDebugAndroidTest`, `adb install`, launch, CameraX/orientation acceptance,
memory profiling, and Galaxy S10e calibration remain untested without the phone.
