**TatraPeak** to niezależny od platformy projekt C++20 do geometrycznej identyfikacji szczytów górskich. Pierwszy obszar danych: Tatry po obu stronach granicy.

Zaimplementowany zakres T01–T17 obejmuje geometrię, kamerę, teren, widoczność,
silnik, JSON/SVG, przygotowanie i walidację danych oraz pierwsze symulowane widoki
na rzeczywistym pakiecie Tatr, interpolację orientacji, deterministyczny replay i
metryki reprojekcji. T19 dodaje benchmarki oraz raport IMG_9553. T20 domyka
wersję demonstracyjną i przenośny most C ABI dla Androida/iOS. T18 ma lokalne
próby zdjęciowe, ale wymaga domknięcia pełnej walidacji terenowej.

- [Architektura i kontrakty C++](docs/MVP_PLAN.md)
- [Zadania T01–T20 z testami odbiorowymi](docs/MVP_TASKS.md)
- [Stan implementacji](docs/IMPLEMENTATION_STATUS.md)

Do rozpoczęcia potrzebujesz kompilatora C++20, CMake >=3.28 i Ninja.
Pierwsza konfiguracja pobiera przypięte zależności przez HTTPS; wykonanie testów
nie wymaga internetu ani danych Tatr. Na Windowsie uruchom Ninja w środowisku
Visual Studio Developer PowerShell z kompilatorem MSVC albo użyj generatora VS.

~~~sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
./build/debug/tatra --help
~~~

Dostępne presety: debug, release, asan (GCC/Clang na Unix), desktop-debug
i desktop-release (z benchmarkami T19) oraz mobile-core (bez CLI/GDAL/JSON).
Preset desktop-debug dodatkowo wymaga zainstalowanej biblioteki GDAL z plikami
nagłówkowymi; nlohmann/json jest pobierane przez CMake. Adaptery obsługują sceny,
GeoTIFF, katalog OSM, manifest pakietu, wynik JSON i wizualizację SVG.

~~~sh
cmake --preset asan
cmake --build --preset asan --parallel 2
ctest --preset asan
~~~

Przy pracy offline można wskazać istniejące źródła zależności standardowymi
opcjami FetchContent, np. FETCHCONTENT_SOURCE_DIR_EIGEN,
FETCHCONTENT_SOURCE_DIR_GEOGRAPHICLIB i FETCHCONTENT_SOURCE_DIR_GOOGLETEST.
Po pierwszym skonfigurowaniu danego katalogu kompilacja korzysta z lokalnych źródeł.

Struktura odpowiada przepływowi danych:

~~~text
include/tatra/        publiczne struktury i kontrakty rdzenia
src/                  geometria, kamera, teren, widoczność i silnik
adapters/desktop/     pliki GeoTIFF/JSON, SVG i przygotowanie pakietu
adapters/mobile/      C ABI bez zależności od Android/iOS SDK
apps/tatra/           program analizy scen i replay
apps/prepare/         narzędzie przygotowania danych
tools/evaluation/     pomiar błędu projekcji
tests/                testy startowe i miejsce na fixture
benchmarks/           benchmarki, fixture IMG_9553 i raporty T19
examples/             przykładowe dane sceny i syntetycznej sesji
docs/                 szczegółowy plan i status
data/                 lokalne dane; ignorowane przez Git
~~~

Target tatra_core nie zależy od SDK telefonu, GDAL ani JSON. Adaptery są osobną,
domyślnie wyłączoną biblioteką tatra_io. Silnik otrzymuje teren i katalog przez
const referencje; właściciel danych musi przeżyć silnik.

Najkrótsze demo po konfiguracji `desktop-release`:

~~~sh
cmake --build --preset desktop-release --target tatra-demo
~~~

Opis wersji pokazowej i artefaktów: [docs/T20_RELEASE.md](docs/T20_RELEASE.md).
Build oraz kontrakt integracji Android/iOS:
[docs/MOBILE_PORTABILITY.md](docs/MOBILE_PORTABILITY.md).

Pierwsze demo na rzeczywistym pakiecie opisuje
[docs/T14_DEMO.md](docs/T14_DEMO.md). Po przygotowaniu danych pojedyncze polecenie
`tatra render` zapisuje `result.json` i `overlay.svg`.

Replay sesji oraz generator kontrolowanego obrotu opisuje
[docs/T16_REPLAY.md](docs/T16_REPLAY.md). Najkrótsza próba bez danych Tatr:

~~~sh
./build/desktop-debug/tatra generate-session --out out/session-control
./build/desktop-debug/tatra replay \
  --dataset synthetic --session out/session-control --out out/replay-control
~~~

Kontrakt metryk, w tym jawne liczenie braków i predykcji spoza obrazu, opisuje
[docs/T17_METRICS.md](docs/T17_METRICS.md).

Instrukcja pomiarów: [benchmarks/README.md](benchmarks/README.md).
Wyniki czasu, pamięci i zgodności po optymalizacji:
[docs/T19_PERFORMANCE.md](docs/T19_PERFORMANCE.md).

Zdjęcia, DEM i katalog szczytów nie są dołączone. Pozyskiwanie i warunki użycia
danych opisuje plan. Wartości w examples/ są syntetyczne lub demonstracyjne.
