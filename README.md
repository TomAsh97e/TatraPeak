**TatraPeak** to niezależny od platformy projekt C++20 do geometrycznej identyfikacji szczytów górskich. Pierwszy obszar danych: Tatry po obu stronach granicy.

Repozytorium jest obecnie kompilowalnym szkieletem do nauki i realizacji MVP.
Działa konfiguracja CMake, biblioteka, testy startowe oraz pomoc/version CLI.
Algorytmy, parsery, renderowanie i przygotowanie danych czekają na implementację.

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

Dostępne presety: debug, release, asan (GCC/Clang na Unix) i desktop-debug.
Preset desktop-debug dodatkowo wymaga zainstalowanej biblioteki GDAL z plikami
nagłówkowymi; nlohmann/json jest pobierane przez CMake. Włączenie adapterów
sprawdza ich kompilację, nie implementuje jeszcze odczytu danych.

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
apps/tatra/           program analizy scen i replay
apps/prepare/         narzędzie przygotowania danych
tools/evaluation/     pomiar błędu projekcji
tests/                testy startowe i miejsce na fixture
benchmarks/           miejsce na pomiary po T19
examples/             przykładowe dane sceny i syntetycznej sesji
docs/                 szczegółowy plan i status
data/                 lokalne dane; ignorowane przez Git
~~~

Target tatra_core nie zależy od SDK telefonu, GDAL ani JSON. Adaptery są osobną,
domyślnie wyłączoną biblioteką tatra_io. Silnik otrzymuje teren i katalog przez
const referencje; właściciel danych musi przeżyć silnik.

W szkielecie funkcje zwracające Result zgłaszają ErrorCode::NotImplemented.
Pozostałe niewdrożone operacje rzucają std::logic_error z numerem zadania.
Nie zwracają fikcyjnych poprawnych wyników. Ten mechanizm jest tymczasowy;
przy implementacji zastąp go kontraktem odpowiedniego zadania i usuń test
Scaffold.UnfinishedEngineDoesNotReportSuccessfulAnalysis po wdrożeniu T07.

Zacznij od T02: walidacji typów w src/validation.cpp. Potem zaimplementuj T03
(geodezję) i T04 (kamerę) wraz z niezależnymi testami liczbowymi. T01 ma już
infrastrukturę i działający przykład degreesToRadians/radiansToDegrees.

Zdjęcia, DEM i katalog szczytów nie są dołączone. Pozyskiwanie i warunki użycia
danych opisuje plan. Wartości w examples/ są syntetyczne lub demonstracyjne.
