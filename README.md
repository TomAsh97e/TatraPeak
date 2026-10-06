# TatraPeak

[![C++ build and tests](https://github.com/TomAsh97e/TatraPeak/actions/workflows/ci.yml/badge.svg)](https://github.com/TomAsh97e/TatraPeak/actions/workflows/ci.yml)

TatraPeak to system C++20 do **geometrycznej identyfikacji szczytów górskich**.
Na podstawie pozycji obserwatora, orientacji i parametrów kamery projektuje szczyty
na obraz oraz sprawdza ich widoczność względem cyfrowego modelu terenu. Pierwszy
kompletny zbiór danych obejmuje Tatry.

Projekt nie rozpoznaje obrazu przez ML. Wynik pochodzi z jawnego modelu
geodezyjnego i optycznego, który można testować niezależnie od telefonu.

## Demo

<p align="center">
  <a href="docs/media/tatrapeak-demo-en.mp4?raw=1">
    <img src="docs/media/tatrapeak-demo-en.jpg" alt="TatraPeak Android application identifying mountain peaks" width="100%">
  </a>
</p>

<p align="center">
  <a href="docs/media/tatrapeak-demo-en.mp4?raw=1">▶ Watch the 20-second demo</a>
</p>

## Co jest gotowe

- przenośny rdzeń C++20: WGS84, ECEF, ENU, kwaterniony i kamera pinhole;
- interpolowany raster wysokości oraz ocena przesłaniania przez teren;
- przygotowanie i walidacja danych Copernicus GLO-30, EGM2008 i OSM;
- desktopowe renderowanie do JSON/SVG oraz deterministyczny replay sesji;
- metryki błędu reprojekcji i benchmarki na scenie fotograficznej;
- wersjonowane C ABI bez eksportowania typów STL/Eigen;
- aplikacja Android w Kotlinie z CameraX, GPS, IMU, JNI i overlayem;
- offline TATRAPK v1 z SHA-256 i walidacją przed przekazaniem danych do C++;
- CI dla Linuxa, Windowsa, macOS, Androida i cross-buildu iOS.

[Przykładowy overlay SVG](examples/artifacts/synthetic-overlay.svg) ·
[Replay GIF](examples/artifacts/replay.gif) ·
[Raport próby IMG_9553](examples/artifacts/quality-report-img9553.json)

## Architektura

Zależności są skierowane do środka. Rdzeń nie zna JSON, GeoTIFF, systemu plików
ani SDK telefonu. Adaptery dostarczają mu zwalidowane dane w pamięci.

~~~mermaid
flowchart LR
    RAW["DEM + geoid + OSM"] --> PREP["tatra-prepare"]
    PREP --> PACK["walidowany tatras-v1"]
    PACK --> IO["desktop IO"]
    PACK --> TPK["TATRAPK v1"]

    subgraph CORE["TatraPeak::core"]
        GEO["WGS84 / ECEF / ENU"]
        CAMERA["kamera pinhole"]
        TERRAIN["GridTerrain"]
        VIS["widoczność"]
        ENGINE["PeakEngine"]
        GEO --> ENGINE
        CAMERA --> ENGINE
        TERRAIN --> VIS --> ENGINE
    end

    IO --> CORE
    TPK --> ANDROID["Kotlin loader"]
    ANDROID --> JNI["JNI → C ABI"] --> CORE
    CORE --> RESULT["projekcje + stany widoczności"]
    RESULT --> SVG["JSON / SVG / metryki"]
    RESULT --> UI["overlay Android"]
~~~

| Warstwa | Odpowiedzialność |
|---|---|
| TatraPeak::core | geometria, kamera, teren, widoczność, silnik, oś czasu |
| TatraPeak::evaluation | błąd reprojekcji w pikselach i stopniach |
| TatraPeak::io | JSON, GeoTIFF, SVG, manifest i przygotowanie danych |
| TatraPeak::mobile | stabilne, platformowo neutralne C ABI |
| tatra | render i replay |
| tatra-prepare | DEM, katalog OSM, manifest i TATRAPK |
| android/ | CameraX, sensory, lokalizacja, JNI i UI |

## Jak działa analiza klatki

1. Pozycje WGS84 są zamieniane na ECEF i lokalny układ ENU.
2. Kwaternion camera_from_enu przenosi punkt do układu kamery.
3. Model pinhole liczy piksel: u = fx·x/z+cx, v = fy·y/z+cy.
4. Punkt otrzymuje stan InFrame, OutsideFrame lub BehindNearPlane.
5. Dla punktów w kadrze promień obserwator–szczyt jest próbkowany względem DEM.
6. Widoczność ma jawny stan: VisibleInModel, OccludedInModel, Uncertain
   albo MissingTerrain.

Geometria używa double, ponieważ ECEF operuje na wartościach rzędu milionów
metrów. Raster używa Float32, aby ograniczyć pamięć, a interpolacja przechodzi
do double. Brak terenu jest stanem, nigdy umowną wysokością zero.

Dataset posiada teren i katalog. PeakEngine pożycza je przez stałe referencje,
a FrameResult posiada własne dane. Błędy C++ są zwracane jako
std::variant<T, Error>; C ABI mapuje je na stabilne statusy i komunikaty.

## Android

| Element | Konfiguracja |
|---|---|
| Język | Kotlin 2.1.20 |
| Build | Gradle 8.11.1, AGP 8.9.2 |
| Android | minSdk 26, target/compileSdk 35 |
| Native | NDK 27.2.12479018, CMake 3.31.6, JNI |
| Kamera | CameraX 1.4.2 + Camera2 interop |
| ABI | release: arm64-v8a; debug: także x86_64 dla emulatora |
| Telefon docelowy | Samsung Galaxy S10e |

Aplikacja nie zakłada cameraId="0". Wybiera tylny obiektyw 20–35 mm
ekwiwalentu najbliższy 26 mm, żąda zoomu 1× i próbuje wyłączyć EIS/OIS.
Intrinsics pochodzą z LENS_INTRINSIC_CALIBRATION albo jawnego fallbacku
z ogniskowej i wymiarów sensora. Crop, obrót i transformacja PreviewView są
przenoszone przez macierze i testowane dla obrotów 0/90/180/270°.

Orientacja korzysta z TYPE_ROTATION_VECTOR. Próbka IMU musi znajdować się
w granicy 25 ms od timestampu klatki; transformacja uwzględnia osie urządzenia,
SENSOR_ORIENTATION, obrót bufora i deklinację magnetyczną. GPS dostarcza
wysokość elipsoidalną WGS84. Pomiar jest odrzucany po 15 s, przy błędzie XY
powyżej 30 m lub błędzie Z powyżej 50 m. Dostępna jest także pozycja ręczna.

Evaluate pracuje poza UI, maksymalnie 8 razy/s, z backpressure. JNI ma barierę
wyjątków, nieprzezroczyste uchwyty i deterministyczne close. Interfejs pokazuje
podgląd, etykiety, stany widoczności, GPS, pozycję ręczną oraz diagnostykę.
Nagranie zapisuje do 10 s / 100 klatek w formacie replay; nie zapisuje filmu.

Pakiet TATRAPK v1 zawiera geometrię rastra, Float32 wysokości, maskę UInt8,
katalog UTF-8, hash manifestu i hash całego pliku. Aktualny lokalny pakiet ma
46 670 890 bajtów, raster 2160×4321 i 50 szczytów. Duże dane są ignorowane
przez Git; bez pakietu aplikacja uruchamia deterministyczną scenę syntetyczną.

## Technologie

C++20, CMake, Ninja, CTest, Eigen, GeographicLib, GDAL, nlohmann/json,
GoogleTest, Kotlin, JNI, CameraX, Camera2, Android SensorManager,
LocationManager, SHA-256 i GitHub Actions.

Zależności C++ pobierane przez FetchContent są przypięte do rewizji i SHA-256.
GDAL oraz JSON należą wyłącznie do opcjonalnej warstwy desktopowej.

## Testy

Ostatnia lokalna weryfikacja:

| Zakres | Wynik |
|---|---:|
| C++ desktop-debug, w tym prawdziwy tatras-v1 | **81/81** |
| C++ mobile-core, bez GDAL/JSON | **34/34** |
| Android JVM/JNI Debug | **26/26** |
| Android JVM/JNI Release | **26/26** |
| Android assembleDebug i lintDebug | passed |
| APK testów instrumentalnych | zbudowane |

Testy obejmują geometrię, granice obrazu, kwaterniony, interpolację terenu,
NoData, widoczność, walidację GeoTIFF/JSON/manifestu, integralność TATRAPK,
deterministyczny replay, metryki, C ABI, JNI, intrinsics, orientację i politykę GPS.
Publiczne nagłówki C++ są kompilowane osobno, a CI uruchamia także ASan/UBSan.

Testy instrumentalne, kalibracja kamery, osie sensorów i pamięć nie zostały jeszcze
sprawdzone na fizycznym Galaxy S10e. Emulator nie zastępuje tego odbioru.

## Próby i wydajność

Pierwszy test rzeczywistego terenu renderuje cztery kierunki z symulowanej pozycji,
sprawdza ruch projekcji, granice pokrycia oraz promienie przechodzące przez styk
kafli DEM.

W próbie fotograficznej IMG_9553 korekta wysokości szczytów zmniejszyła błędy
dwóch punktów kontrolnych z 254,60 do 65,57 px oraz z 193,36 do 92,68 px.
Wynik wskazuje wpływ wysokości, ale nie rozstrzyga błędu pozycji, ogniskowej,
oznaczeń ani rozdzielczości DEM. Pełna walidacja terenowa nadal jest otwarta.

Benchmark siedmiu naprzemiennych par wykazał poprawę mediany sampleHeight
o **8,18%** kosztem 32 bajtów na raster. Pełne evaluate pozostało praktycznie
bez zmian: **0,2444 → 0,2446 ms** na testowym komputerze. Odrzucono więc bardziej
złożony cache, SIMD i własny alokator.

## Szybki start

Wymagania bazowe: kompilator C++20, CMake ≥3.28 i Ninja. Pierwsza konfiguracja
może pobrać przypięte zależności; późniejsze testy działają offline.

~~~sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
~~~

Demo generujące result.json i overlay.svg:

~~~sh
cmake --preset desktop-release
cmake --build --preset desktop-release --target tatra-demo
~~~

Android wymaga JDK 17 lub 21 i Android SDK. Otwórz katalog android/
w Android Studio albo uruchom:

~~~sh
cd android
./gradlew test assembleDebug
./gradlew connectedDebugAndroidTest  # emulator lub podłączony telefon
~~~

APK: android/app/build/outputs/apk/debug/app-debug.apk.

## Ograniczenia

- wymagana jest znana pozycja, wysokość, orientacja i kalibracja kamery;
- model pinhole nie koryguje dystorsji, rolling shutter ani zmian stabilizacji;
- DEM nie opisuje aktualnych drzew, budynków, chmur ani śniegu;
- VisibleInModel oznacza widoczność w modelu, nie obserwację rzeczywistą;
- komenda CLI tatra evaluate nie ma jeszcze kompletnego adaptera JSON;
- pełna walidacja terenowa oraz odbiór na Galaxy S10e pozostają do wykonania;
- wspólne C ABI cross-kompiluje się dla iOS, ale aplikacja Swift jeszcze nie istnieje.

## Mapa repozytorium

~~~text
include/tatra/        publiczne kontrakty C++ i C ABI
src/                  przenośny rdzeń
adapters/desktop/     JSON, GeoTIFF, SVG i przygotowanie danych
adapters/mobile/      platformowo neutralne C ABI
apps/                 programy tatra i tatra-prepare
tests/                testy jednostkowe i integracyjne
benchmarks/           pomiary i raport T19
android/              aplikacja Kotlin/JNI
examples/             wejścia i publiczne artefakty demo
docs/                 szczegółowe raporty etapów i kontrakty
~~~

Dokumenty pogłębione:
[architektura C++](docs/MVP_PLAN.md),
[przenośność](docs/MOBILE_PORTABILITY.md),
[Android](docs/ANDROID_PROGRESS.md),
[format TATRAPK](docs/ANDROID_DATA_FORMAT_PROPOSAL.md),
[próby fotograficzne](docs/T18_HEIGHT_TRIAL.md) i
[wydajność](docs/T19_PERFORMANCE.md).
