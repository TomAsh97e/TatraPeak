# Android MVP — stan na 2026-10-06

Nie wykonano testów na telefonie, zgodnie z decyzją o kontynuowaniu bez podłączania
Samsunga Galaxy S10e. Wyniki sprzętowe poniżej są oznaczone jako niewykonane.

## Etap 1 — build i JNI

Gotowe na komputerze:

- projekt Gradle/Kotlin, minSdk 26, tylko `arm64-v8a`, przypięty NDK 27.2.12479018;
- `libtatra_jni.so` linkuje `TatraPeak::mobile`; CLI, desktop IO, testy CMake i benchmarki
  są wyłączone w buildzie APK;
- `NativeEngine : Closeable`, create/evaluate/destroy, statusy C ABI i bariera wyjątków;
- prawdziwy JNI smoke test odpowiadający `mobile_c_api_test.c`, uruchamiany na JVM hosta;
- ekran diagnostyczny pokazuje wynik syntetyczny.

Pliki: `android/settings.gradle.kts`, `android/build.gradle.kts`,
`android/app/build.gradle.kts`, `android/app/src/main/cpp/`,
`android/app/src/main/java/pl/tatrapeak/engine/`, `SyntheticScene.kt`,
`NativeEngineTest.kt`, `MainActivity.kt`.

Telefon: instalacja, uruchomienie, test JNI arm64 i pomiar wycieku — niewykonane.

## Etap 2 — kamera

Gotowe na komputerze:

- CameraX `PreviewView` z `FIT_CENTER`, ImageAnalysis i przezroczystym overlay;
- wybór tylnego kandydata 1× z ogniskowej i rozmiaru sensora, bez stałego cameraId;
- żądanie physical camera ID, zoom 1× oraz wyłączenie EIS/OIS, z pokazaniem
  faktycznych metadanych odpowiedzi;
- jawne `bufferToView = sensorToView * inverse(sensorToBuffer)`;
- testy rogów, środka, cropu i wszystkich obrotów ćwierćobrotowych.

Pliki: `camera/CameraController.kt`, `camera/ImageGeometry.kt`, `ui/OverlayView.kt`,
`ImageGeometryTest.kt`, manifest i zależności CameraX.

Telefon: wybór głównego obiektywu, zgodność punktów overlay i obrót ekranu — niewykonane.

## Etap 3 — intrinsics

Gotowe na komputerze:

- preferowane `LENS_INTRINSIC_CALIBRATION`;
- jawny fallback z ogniskowej, rozmiaru sensora i pixel array;
- przeliczenie fx/fy/cx/cy przez sensor→buffer, crop i obrót;
- źródło i przybliżenia są widoczne w diagnostyce; skew lub niezgodne active arrays
  blokują użycie zamiast tworzyć pozornie poprawny wynik.

Pliki: `camera/CameraIntrinsics.kt`, `camera/Intrinsics.kt`, `IntrinsicsTest.kt`.

Telefon: wiarygodność metadanych, crop ISP, dystorsja i kalibracja — niewykonane.

## Etap 4 — orientacja

Gotowe na komputerze:

- `TYPE_ROTATION_VECTOR`, timestamp próbek i bufor dopasowania do klatki (25 ms);
- jawna macierz ENU→kamera, SENSOR_ORIENTATION, obrót bufora i deklinacja;
- kwaternion w,x,y,z i diagnostyczne heading/pitch/roll;
- niezależne testy północ, wschód, zenit, landscape, roll oraz znak deklinacji;
- pełny opis konwencji w `ANDROID_ORIENTATION.md`.

Pliki: `sensors/OrientationMath.kt`, `sensors/OrientationTracker.kt`,
`OrientationTest.kt`, `docs/ANDROID_ORIENTATION.md`.

Telefon: poprawność osi, timestampów i wskazań na Galaxy S10e — niewykonane.

## Etap 5 — lokalizacja

Gotowe na komputerze:

- CAMERA oraz FINE/COARSE w manifeście i obsługa odmowy;
- bieżące aktualizacje GPS bez bezwarunkowego lastLocation;
- wysokość elipsoidalna `Location.altitude`, hasAltitude i dokładności;
- odrzucenie wieku >15 s, błędu XY >30 m i błędu Z >50 m;
- ręczna pozycja testowa, zachowana po odtworzeniu Activity;
- deklinacja przez `GeomagneticField` i wstrzymanie evaluate przy brakujących danych;
- evaluate maksymalnie 8 razy/s poza UI, z backpressure i publikacją wyniku na UI.

Pliki: `location/LocationPolicy.kt`, `location/LocationTracker.kt`,
`LocationPolicyTest.kt`, `engine/FrameEvaluator.kt`, `MainActivity.kt`.

Telefon: uprawnienia, GPS, deklinacja i zachowanie bez sensorów — niewykonane.

## Etap 6 — dane offline

Gotowe na komputerze:

- zatwierdzony format TATRAPK v1 i polecenie `tatra-prepare mobile`;
- konwerter najpierw ładuje i waliduje istniejący pakiet `tatras-v1`;
- loader Androida przed JNI sprawdza długość, wersję, overflow, rezerwy, SHA-256,
  UTF-8, maskę, wymiary, rekordy i końcowe bajty;
- wygenerowany lokalny asset ma około 45 MiB, raster 2160×4321, 50 szczytów,
  SHA-256 `5e6ca2e3405d45ad0dc4781b2ac70228fdfda4baa9a33aff3d5a6eab5f5e4e1b`;
- asset jest niekompresowany w APK i ignorowany przez Git; brak assetu daje jawny
  fallback do fixture syntetycznego.

Pliki: `adapters/desktop/mobile_package_writer.cpp`, `io.hpp`, `apps/prepare/main.cpp`,
`data/MobilePackage.kt`, `MobilePackageTest.kt`, `ANDROID_DATA_FORMAT_PROPOSAL.md`.

Telefon: szczytowe zużycie pamięci loadera i czas utworzenia silnika — niewykonane.

## Etap 7 — działający overlay

Gotowe na komputerze:

- evaluate działa na pojedynczym workerze z backpressure, maksymalnie 8 razy/s;
- wynik trafia na UI, a overlay pomija OutsideFrame/BehindNearPlane;
- nazwa, opcjonalna wysokość i pięć stanów widoczności mają odrębne kolory;
- CameraX, sensory, GPS i silnik są zamykane w lifecycle;
- przycisk nagrywa do 10 s / 100 klatek do istniejącego formatu replay T16.

Pliki: `engine/FrameEvaluator.kt`, `ui/OverlayView.kt`, `ui/MainActivity.kt`,
`data/DiagnosticRecorder.kt`.

Telefon: czytelność etykiet, wydajność 5–10 Hz, lifecycle i replay nagrania — niewykonane.

## Weryfikacja na komputerze

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

APK aplikacji: `android/app/build/outputs/apk/debug/app-debug.apk` (arm64-v8a).
APK testowe: `android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk`.
`libtatra_jni.so` zależy tylko od bibliotek systemowych Android (`libm`, `libdl`,
`libc`); GDAL i nlohmann/json nie weszły do APK.

`connectedDebugAndroidTest`, `adb install`, uruchomienie, odbiór CameraX/orientacji,
profilowanie pamięci i kalibracja na Galaxy S10e pozostają niewykonane bez telefonu.
