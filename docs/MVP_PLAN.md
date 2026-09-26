**TatraPeak: plan niezależnego od platformy MVP w C++20.**

To specyfikacja do implementacji, nie gotowa biblioteka. Podane polecenia tatra i tatra-prepare oznaczają projektowane programy. Szczegółowe zadania i kryteria ukończenia znajdują się w [MVP_TASKS.md](MVP_TASKS.md).

MVP oblicza, jakie znane szczyty powinny znajdować się w obrazie kamery o zadanej pozycji, orientacji i parametrach. Wynikiem są współrzędne podpisów, dane szczytów i ocena przesłaniania przez model terenu. Pierwsze wejścia pochodzą z plików. Wszystkie obliczenia działają bez telefonu i po przygotowaniu danych także bez internetu.

Używaj słowa „identyfikacja geometryczna”: ta wersja nie rozpoznaje dowolnego zdjęcia bez informacji o miejscu wykonania i kierunku. AI, automatyczne dopasowanie panoramy, SDK telefonów, filtrowanie surowego IMU, GPU i wielowątkowość są rozszerzeniami po ukończeniu opisanej wersji.

**Produktem MVP są trzy narzędzia współdzielące kod.**

| Element | Wejście | Wyjście |
|---|---|---|
| tatra_core | Dane w pamięci: teren, katalog, obserwator, kamera i orientacja | Wynik analizy klatki |
| tatra | Pakiet regionu i scenariusz JSON lub sesja | JSON z wynikami, SVG, raport ewaluacji |
| tatra-prepare | Surowy DEM, model geoidy i eksport OSM | Ujednolicony pakiet regionu z metadanymi |

Przykładowe docelowe wywołania:

~~~sh
tatra render --dataset data/tatras-v1 --scene examples/scene.json --out out/scene
tatra replay --dataset data/tatras-v1 --session examples/session --out out/replay
tatra evaluate --predictions out/scene/result.json --annotations examples/annotations.json --out out/metrics.json
~~~

Render zapisuje result.json i overlay.svg. SVG można otworzyć w przeglądarce. W wariancie ze zdjęciem korzysta ze zwykłej fotografii perspektywicznej i zgodnych rozmiarów obrazu; zdjęcie jest opcjonalnym tłem. Nagranie wideo nie jest wymagane do pierwszego replay: wystarczą czasy klatek i próbki orientacji.

**Zależności mają służyć konkretnym warstwom.**

| Zależność | Użycie |
|---|---|
| C++20, CMake, CTest | Kompilacja biblioteki, aplikacji i testów |
| Eigen | Wektory, macierze, kwaterniony |
| GeographicLib | WGS84/ECEF/ENU; w narzędziu przygotowania także geoidy |
| GDAL | Wyłącznie odczyt i przygotowanie GeoTIFF w adapterze desktopowym |
| nlohmann/json | Wyłącznie adapter plików JSON |
| GoogleTest | Testy funkcji, integracji i błędów wejściowych |
| Google Benchmark | Pomiar geometrii i widoczności po działającej wersji |

OpenCV nie jest wymagane do projekcji pinhole i generowania SVG. Można później użyć go w osobnym narzędziu do kalibracji aparatu lub prostowania zdjęć.

Przypnij sprawdzone wersje zależności przy implementacji. tatra_core może zależeć od Eigen i GeographicLib, lecz nie od GDAL, JSON, systemu plików, GUI ani SDK telefonu. Udostępnij opcję CMake TATRA_BUILD_DESKTOP_IO=OFF, aby budować rdzeń i testy syntetyczne bez GDAL.

**Przepływ zależności jest jednokierunkowy.**

~~~mermaid
flowchart TD
    RAW["Surowy DEM + OSM + geoid"] --> PREP["tatra-prepare"]
    PREP --> PACK["Pakiet regionu"]
    PACK --> IO["tatra_io: odczyt i walidacja"]
    SCENE["Scena / sesja JSON"] --> IO
    IO --> OWN["Aplikacja: właściciel danych"]
    OWN --> ENGINE["PeakEngine"]
    ENGINE --> GEO["geo + camera"]
    ENGINE --> VIS["visibility"]
    VIS --> TERRAIN["ITerrain / GridTerrain"]
    ENGINE --> RESULT["FrameResult"]
    RESULT --> SVG["SVG / JSON / ewaluacja"]
    SYNTH["Syntetyczne dane testowe"] --> ENGINE
~~~

Proponowany podział plików:

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
  data/                 # pobrane duże dane poza repo
~~~

Aplikacja jest właścicielem Dataset. Dataset posiada katalog i GridTerrain. PeakEngine przechowuje stałe referencje do nich, więc nie może przeżyć Dataset. Dane nie zmieniają się podczas obliczeń. FrameResult posiada własny wektor wyników i identyfikatory szczytów; nie zawiera wskaźników do tymczasowych obiektów.

W tym MVP zwykły wektor katalogu i liniowe filtrowanie są wystarczającym punktem startu dla kilkudziesięciu lub kilkuset szczytów. Indeks przestrzenny dodaj, gdy pomiary pokażą potrzebę.

Wartości geometryczne przechowuj w double. Współrzędne ECEF mają rząd milionów metrów; float może tracić istotną precyzję przy odejmowaniu pobliskich dużych wartości. Float w rastrze oszczędza pamięć, a interpolację wykonujesz już w double.

vector posiada dane, a span i referencje tylko je pożyczają: nie przedłużają czasu życia. W tym synchronicznym przepływie nie potrzebujesz współdzielonej własności przez shared_ptr. Interfejs wirtualny ITerrain ma uzasadnienie, ponieważ podmieniasz źródło wysokości; zwykłych funkcji matematycznych nie trzeba zamieniać w hierarchię klas.

**Konwencje są częścią API i muszą mieć testy.**

| Pojęcie | Konwencja |
|---|---|
| Geografia | WGS84, kolejność latitude, longitude; w nazwach pól jawne deg |
| Wysokość geometrii | Metry nad elipsoidą WGS84 |
| ECEF | Globalny kartezjański układ WGS84, metry |
| ENU | x: wschód, y: północ, z: góra; początek w obserwatorze |
| Kamera | x: prawo, y: dół, z: przed kamerą |
| Obraz | u w prawo, v w dół; lewy górny brzeg obrazu: (0, 0); środki pikseli: (i+0.5, j+0.5) |
| Heading | Stopnie zgodnie z ruchem wskazówek zegara od północy geograficznej |
| Pitch | Dodatni: kamera patrzy w górę |
| Roll | Dodatni obraca oś „prawo kamery” w stronę osi „dół kamery” przy stałym kierunku patrzenia |
| Kwaternion | camera_from_enu: mnoży wektor ENU i daje wektor kamery |
| Czas | Int64: nanosekundy od początku sesji, wspólna podstawa czasu |

Heading z pliku jest względem północy geograficznej. Przyszły adapter telefonu będzie odpowiadał za korektę północy magnetycznej i mapowanie osi urządzenia. Biblioteka nie może zgadywać, co oznaczają dane wejściowe.

Dla heading=pitch=roll=0 kierunek patrzenia to północ. Wtedy:

~~~text
camera_x = ENU_east
camera_y = -ENU_up
camera_z = ENU_north
~~~

Przy tworzeniu obrotu z heading ψ i pitch θ można zbudować bazę, licząc kąty w radianach:

~~~text
forward = (sin ψ cos θ, cos ψ cos θ, sin θ)
right   = (cos ψ, -sin ψ, 0)
down    = forward × right

right_rolled = cos ρ * right + sin ρ * down
down_rolled  = -sin ρ * right + cos ρ * down

R_camera_from_enu ma wiersze:
right_rolled^T, down_rolled^T, forward^T
~~~

To usuwa niejasność kolejności mnożenia macierzy Eulera. W stanie silnika przechowuj kwaternion, a heading/pitch/roll traktuj jako wygodne wejście scenariusza.

**Poniższe deklaracje określają kontrakt; nie są jeszcze implementacją.**

W C++20 nie ma std::expected. Dla tego projektu wystarczy std::variant<T, Error>. std::optional oznacza rzeczywisty brak wartości, np. brak katalogowej wysokości, a nie błąd, którego przyczynę trzeba poznać.

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
using PeakId = std::string;  // np. "osm:node:123"; syntetyczne: "test:north"

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

// Osobne typy blokują przypadkowe przekazanie ENU jako ECEF.
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
    std::string vertical_reference; // może być "unspecified"; nie używać wtedy do geometrii
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

Wysokości rozdzielamy świadomie. Katalog OSM może zawierać „ele”, ale nie zawsze wiarygodnie określa układ wysokości. W pierwszym pakiecie pozycja geometryczna szczytu otrzymuje wysokość z ujednoliconego DEM. Wysokość wyświetlana pochodzi z katalogu i ma osobne pochodzenie. To przybliżenie modelu 30 m: nie obiecuj dokładnego położenia małej turni.

W wejściu sceny można ułatwić podanie obserwatora jako wysokości nad DEM, np. 2 m. Adapter zamienia ją na GeoPosition. To wirtualny obserwator nad powierzchnią modelu, nie dokładny pomiar położenia człowieka, szczególnie w DSM z drzewami.

Waliduj: zakresy współrzędnych, skończoność liczb, dodatnie ogniskowe, dodatnie rozmiary obrazu i poprawny kwaternion. Odrzuć zerowy kwaternion; dla niewielkiego odchylenia normy od 1 można normalizować według jawnej polityki. Macierzy obrotu nie zastępuj dowolną macierzą bez sprawdzenia.

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
    // Prywatnie: lokalny układ GeographicLib.
};

struct BearingElevation {
    double azimuth_deg;     // [0, 360)
    double elevation_deg;   // [-90, 90]
    double slant_range_m;   // długość wektora, nie dystans po szlaku
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
    std::optional<PixelPoint> pixel; // wartość dla z > near_z, także poza obrazem
    double camera_depth_m;
};

Projection project(
    const EnuPoint& point,
    const CameraPose& pose,
    const CameraIntrinsics& camera,
    double near_z_m);

} // namespace tatra
~~~

Funkcja project przyjmuje uprzednio zwalidowane parametry. Liczy p_camera = q * p_enu. Dla z <= near_z zwraca BehindNearPlane bez dzielenia przez z. W przeciwnym razie:

~~~text
u = fx * x / z + cx
v = fy * y / z + cy
~~~

InFrame oznacza 0 <= u < width i 0 <= v < height. Funkcja cameraFromHorizontalFov przyjmuje piksele kwadratowe: fx=fy=width/(2*tan(hfov/2)), cx=width/2, cy=height/2. Pełne intrinsics mogą być wczytane bezpośrednio ze sceny.

To model pinhole dla obrazu bez dystorsji albo już wyprostowanego. Oryginalne zdjęcia z szerokokątnego obiektywu mogą wymagać prostowania. Parametry kamery muszą dotyczyć obrazu po obrocie, skalowaniu i przycięciu; samo kopiowanie EXIF nie wystarczy.

Przy imporcie kalibracji z konwencji, w której środek pierwszego piksela to (0,0), przelicz punkt główny i adnotacje do przyjętej tutaj konwencji brzegowej, dodając 0.5 do obu współrzędnych. Nie mieszaj tych dwóch konwencji w jednym raporcie.

**Teren jest jedynym potrzebnym na start interfejsem polimorficznym.**

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
    double latitude_step_deg;   // ujemny dla rastra north-up
    double longitude_step_deg;  // dodatni
};

class GridTerrain final : public ITerrain {
public:
    static Result<GridTerrain> create(
        GridGeometry,
        std::vector<float> heights_ellipsoid_m,
        std::vector<std::uint8_t> valid_mask);

    HeightResult sampleHeight(GeoCoord) const override;
    // Prywatnie: geometria, wysokości, maska.
};

} // namespace tatra
~~~

GridTerrain posiada dane w kolejności row-major: index=row*columns+column. sampleHeight wykonuje interpolację biliniową. Poza pokryciem zwraca OutsideCoverage, a brak wymaganej próbki oznacza NoData. Zero jest prawidłową wysokością, nigdy umownym brakiem danych.

W tej wersji adapter akceptuje wyłącznie raster north-up o współrzędnych poziomych WGS84 i jawnie potwierdzonych wysokościach elipsoidalnych. Inne dane normalizuje tatra-prepare albo odrzuca loader. Żaden adapter nie może tylko zmienić etykiety układu wysokości bez przeliczenia wartości.

GDAL opisuje geotransformację względem brzegu piksela; środek pierwszego piksela leży w (0.5, 0.5). Wylicz first_center z geotransformacji zwracanej przez GDAL. Nie dodawaj drugiej ręcznej korekty na podstawie RasterPixelIsPoint. [Geotransformacja GDAL](https://gdal.org/en/stable/tutorials/geotransforms_tut.html)

Jedna siatka regionu w pamięci wystarczy na MVP. Późniejsze kafelkowanie lub mapowanie pliku może implementować ITerrain bez zmiany geometrii i silnika.

**Widoczność oznacza ocenę w modelu terenu, z jawnymi ograniczeniami próbkowania.**

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

Początkowe wartości są parametrami eksperymentalnymi. Pas 0.5 m nie opisuje dokładności rzeczywistego DEM. VisibleInModel nie oznacza gwarancji, że szczyt jest widoczny na fotografii.

Algorytm referencyjny:

1. Zamień obserwatora O i szczyt P na ECEF; wyznacz D=|P-O|.
2. Sprawdź zgodność punktów końcowych z terenem. Obserwator w modelu pod ziemią lub szczyt wyraźnie poniżej lokalnego DEM oznacza Uncertain.
3. Próbkuj odcinek w ECEF: X(t)=O+t*(P-O), w odstępach nie większych od sample_step_m, po odjęciu jawnych odcinków ochronnych przy końcach. Podziel cały sprawdzany odcinek na N=ceil(length/step) części i użyj ich środków.
4. Każde X zamień na pozycję geograficzną. Odczytaj teren dla jej lat/lon.
5. Policz clearance = wysokość elipsoidalna X - wysokość elipsoidalna terenu.
6. Wartość poniżej -clearance_band_m daje znane przesłonięcie. Zapisz odległość pierwszej wykrytej przeszkody i można zakończyć.
7. Gdy nie ma znanej przeszkody, ale brakuje terenu, zwróć MissingTerrain. Pełna ścieżka nie została sprawdzona.
8. Przy próbkach w paśmie tolerancji lub niewystarczającej długości sprawdzanego odcinka zwróć Uncertain. Dopiero pozostały przypadek to VisibleInModel.

Wynik min_sampled_clearance dotyczy tylko odwiedzonych próbek, także przy wczesnym zakończeniu. Nie nazywaj go ścisłym minimum całej linii. Przekroczenie max_samples daje Uncertain z przyczyną, a nie milczące zwiększenie kroku i pewny wynik.

Guard przy szczycie chroni przed jego własną komórką rastra, lecz może pominąć przeszkodę w tym obszarze. Dobierz go do rozdzielczości pakietu, zbadaj wpływ jego zmiany i zapisz w wyniku. Dla krótkich odcinków, w których wyłączone fragmenty dominują, zwracaj Uncertain. Początkowy zakres kandydatów 0.5–30 km ogranicza część takich przypadków.

Próbkowanie co 10 m nie jest dowodem, że żadna wąska grań nie została pominięta. Testuj zbieżność 10/5/2.5 m. Późniejsze dokładniejsze przecięcie siatki/mesha jest osobnym zadaniem. Promień ECEF uwzględnia krzywiznę geometryczną Ziemi; refrakcja atmosferyczna pozostaje poza MVP.

**Silnik składa gotowe funkcje, a nie implementuje je ponownie.**

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
    // Puste dla szczytu, dla którego nie wykonano kosztownego testu.
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
    // Prywatnie: const ITerrain& oraz const PeakCatalog&.
};

} // namespace tatra
~~~

evaluate:

1. Waliduje FrameQuery. Błędna kamera lub nieskończona liczba to Error całego zapytania.
2. Tworzy LocalFrame obserwatora.
3. Dla każdego szczytu wyznacza ENU i odległość; odrzuca poza zakresem.
4. Oblicza azymut, elewację i project.
5. Dla InFrame wywołuje traceVisibility. Dla pozostałych zostawia visibility puste.
6. Zwraca wszystkie obserwacje z zakresu w stabilnej kolejności id oraz diagnostykę.

Brak terenu na jednej linii widzenia jest wynikiem konkretnego szczytu, nie powodem utraty wszystkich pozostałych wyników. Aplikacja może ostrzec o częściowym pokryciu i pokazać pozostałe dane.

Rdzeń nie decyduje, czy renderować niepewny podpis. SVG domyślnie pokazuje VisibleInModel zwykłym kolorem, Uncertain/MissingTerrain odróżnionym stylem i ukrywa OccludedInModel. Tryb debug pokazuje wszystkie statusy. Zawsze eksportuj pełną ocenę do JSON.

Nie loguj do stdout z geometrii. Nie odczytuj czasu wykonania wewnątrz evaluate. Pomiar czasu należy do benchmarku/CLI i osobnego pliku; to pozwala zachować deterministyczny wynik.

**Replay sprawdza także czas i orientację.**

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
    Observer observer;                 // stały w tej wersji
    CameraIntrinsics camera;           // stałe w tej wersji
    std::vector<PoseSample> poses;
    std::vector<ReplayFrame> frames;
};

} // namespace tatra
~~~

create odrzuca niefinitywne/zerowe obroty oraz ujemne, niemonotoniczne i powtórzone timestampy. sampleAt zwraca próbkę dla dokładnie zgodnego czasu, pomiędzy próbkami wykonuje SLERP najkrótszą drogą, a poza zakresem zwraca błąd. Zbyt duża luka daje TimeGapTooLarge. Nie ekstrapoluj po cichu.

Replay iteruje po zadanych czasach klatek, pobiera poseAt i woła ten sam evaluate. Nie czeka na czas rzeczywisty, nie używa sleep. Błędna klatka otrzymuje zapisany błąd; raport podaje liczbę pominiętych klatek.

W tej wersji pozycja obserwatora jest stała. Interpolacja ruchomej pozycji i surowe akcelerometry/żyroskopy należą do późniejszego rozszerzenia. Timestampy sceny są idealnie zsynchronizowane; przyszły adapter telefonu musi osobno ustalić zgodność zegarów kamery i IMU.

**Adaptery mają małe, jawne kontrakty.**

~~~cpp
// Deklaracje koncepcyjne adaptera desktopowego; typy dataset/scena
// zawierają metadane opisane poniżej.
Result<Dataset> loadDataset(const std::filesystem::path&);
Result<FrameQuery> loadScene(
    const std::filesystem::path&, const ITerrain&);
Result<Session> loadSession(
    const std::filesystem::path&, const ITerrain&);

// std::monostate reprezentuje sukces bez wartości w Result.
Result<std::monostate> writeFrameJson(
    const FrameResult&, const PeakCatalog&,
    const RunMetadata&, const std::filesystem::path&);

Result<std::monostate> writeOverlaySvg(
    const FrameResult&, const PeakCatalog&,
    const RenderOptions&, const std::filesystem::path&);
~~~

JSON/SVG zapisują identyfikator i hash pakietu, wersję programu oraz opcje sceny. SVG musi poprawnie escapować nazwy XML, w tym znaki &, < i >. Pozycja markera oznacza punkt szczytu; tekst można odsunąć i połączyć linią. Testy błędu dotyczą markera, nigdy pozycji tekstu.

Na start wystarczy proste, deterministyczne układanie etykiet: stała kolejność priorytetów, kilka kandydatów na położenie tekstu, odrzucenie kolidujących prostokątów. Gdy nie ma miejsca, ukryj tekst, ale zachowaj marker i pełny rekord JSON.

**Pakiet danych musi być reprodukowalny.**

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
  images/                 # opcjonalne
  annotations.json        # opcjonalne
~~~

manifest.json zapisuje: schema_version, dataset_id, datę pobrania, oryginalne adresy i identyfikatory plików, SHA-256, poziomy układ odniesienia, oryginalny i wynikowy układ wysokości, użyty model geoidy, rozmiary i krok rastra, NoData, obszar pokrycia, sposób resamplingu, wersję narzędzia i wymagane oznaczenia źródeł.

Scenę można opisać tak:

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

To ilustracyjna wirtualna scena, a nie zweryfikowane stanowisko fotograficzne, co
zapisuje jawne `observer.simulated=true`. Dla prawdziwego zdjęcia należy zapisać
rzeczywiste lub jawnie oszacowane współrzędne, intrinsics i orientację. Alternatywą
dla above_dem jest ellipsoid z wartością bezpośrednią; nie dopuszczaj obu naraz.

**Dane dla Tatr przygotuj w następującej kolejności.**

1. Pobierz z [Copernicus Data Space](https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM) GLO-30 DGED/GeoTIFF. Dostęp do GLO-30 jest dostępny dla zarejestrowanych użytkowników. Dla roboczego obszaru DEM west=19.4, south=49.0, east=20.6, north=49.6 wybierz kafle przecinające ten prostokąt. To obszar testowy z zapasem, nie oficjalna granica Tatr.
2. Zachowaj surowe pliki i ich identyfikatory. Połącz je i przytnij przez GDAL; sprawdź pokrycie, NoData i zgodność krawędzi. Nie podnoś sztucznie rozdzielczości 30 m do 1 m.
3. Przelicz wysokości EGM2008 na elipsoidalne WGS84. Pobierz model egm2008-2_5 z [danych GeographicLib](https://geographiclib.sourceforge.io/C++/doc/geoid.html). Dla każdego poprawnego piksela oblicz h=H+N(latitude,longitude), gdzie N daje model geoidy. Zapisz wersję/model i hash. Mniejszy regionalny pakiet wynikowy nie wymaga geoidy w czasie działania silnika.
4. Pobierz węzły natural=peak przez Overpass; zapisuj surowy JSON zamiast pytać API podczas każdego uruchomienia. Alternatywnie użyj ekstraktów PBF [Polski](https://download.geofabrik.de/europe/poland.html) i [Słowacji](https://download.geofabrik.de/europe/slovakia.html), wycinając region i deduplikując po identyfikatorze OSM.
5. Wybierz 30–100 szczytów do pierwszego katalogu. Zachowaj id, lat/lon, name, name:pl, name:sk, ele i źródło. Nie łącz rekordów tylko dlatego, że mają identyczną nazwę.
6. Wysokość geometryczną każdego węzła pobierz z przygotowanego DEM. Brak pokrycia oznacza odrzucenie rekordu z raportem. Brak ele nie usuwa szczytu; UI pokazuje wtedy brak zweryfikowanej wysokości.
7. Zapisz pakiet i raport walidacji. Przy ponownym przygotowaniu tych samych plików otrzymuj tę samą treść danych poza jawnie zmiennymi metadanymi procesu.

GLO-30 jest DSM, czyli modelem powierzchni obejmującym również roślinność i budynki; jego siatka jest kątowa, więc „30 m” nie oznacza kwadratów dokładnie 30×30 m. Jego układ wysokości to EGM2008. Nie traktuj go jak bezbłędnego modelu gruntu ani dokładnego katalogu wysokości wierzchołków. [Specyfikacja Copernicus DEM](https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM)

Przykład zapytania do [Overpass Turbo](https://overpass-turbo.eu/):

~~~text
[out:json][timeout:60];
node["natural"="peak"](49.10,19.55,49.35,20.40);
out body;
~~~

Kolejność bbox Overpass: south, west, north, east. Ten prostokąt katalogu jest mniejszy niż obszar DEM. MVP importuje tylko węzły; odnotuj tę granicę kompletności. Szczegóły: [natural=peak](https://wiki.openstreetmap.org/wiki/Tag:natural%3Dpeak), [Overpass QL](https://wiki.openstreetmap.org/wiki/Overpass_API/Overpass_QL).

W tatra-prepare wydziel funkcje:

~~~cpp
readRawDem(...);
normalizeHeightGridToEllipsoid(...);
parseOsmPeakNodes(...);
validateAndDeduplicatePeaks(...);
assignGeometryHeights(...);
writeDatasetManifest(...);
~~~

Te funkcje należą do narzędzia danych, nie do PeakEngine. Licencja kodu i warunki danych to odrębne rzeczy. Zachowaj oznaczenia Copernicus i [OpenStreetMap](https://www.openstreetmap.org/copyright) w pakiecie oraz w dokumentacji demonstracji.

Po MVP można ocenić polski [NMT](https://www.geoportal.gov.pl/pl/dane/numeryczny-model-terenu-nmt/) i słowackie [DMR](https://www.skgeodesy.sk/gku/produkty-sluzby/na-stiahnutie/zbgis.html?forceBrowserDetector=blind). Łączenie krajowych układów wysokości jest osobnym zadaniem, a nie zwykłym przycięciem rastra.

**Testy mają różne źródła prawdy.**

| Warstwa | Przykład | Co dowodzi |
|---|---|---|
| Analityczna | Znane punkty ECEF, ręcznie policzona projekcja | Poprawność wzorów i konwencji |
| Syntetyczny teren | Płaska powierzchnia, wysoka grań, brak danych | Zachowanie przesłaniania i błędów |
| Adaptery | Mały GeoTIFF i JSON o znanej treści | Poprawność metadanych i odczytu |
| Integracja | Scena z kilkoma sztucznymi szczytami | Współpraca modułów |
| Replay | Obrót z przerwą w próbkach | Interpolacja, czas i powtarzalność |
| Zdjęcia | Ręcznie oznaczone wierzchołki ze znanego miejsca | Zgodność modelu z obrazem |
| Wydajność | Stały dataset, Release, opis komputera | Koszt obliczeń i sens optymalizacji |

Podstawowe niezależne liczby kontrolne:

- WGS84: lat=0, lon=0, h=0 daje ECEF (6378137,0,0) m.
- Przy początku ENU na równiku i lon=0: ECEF +Y oznacza wschód, +Z północ, +X górę.
- Pinhole fx=fy=1000, cx=640, cy=360: punkt kamery (0,0,1000) daje (640,360), a (100,0,1000) daje (740,360).
- Punkty kamery z z<=near_z nie mogą powodować dzielenia przez zero.
- Siatka 2×2 z wysokościami 100,120 / 140,160 daje 130 w środku.
- Próbki orientacji 359° i 1° mają środek blisko 0°, nie 180°.

Test round-trip geodezji jest potrzebny, ale nie jest jedyną kontrolą: dwie błędne, wzajemnie odwrotne transformacje mogą go przejść.

Zdjęcia bierz z własnych archiwów, od znajomych albo z Wikimedia Commons, zachowując autora, adres pliku i warunki użycia. Informacja „zdjęcie przedstawia Giewont” nie dowodzi znajomości miejsca wykonania. Współrzędne na stronie mogą wskazywać fotografowany obiekt, a nie fotografa. [Zasady ponownego użycia plików Commons](https://commons.wikimedia.org/wiki/Commons:Contact_us/Reuse)

Dla każdej sceny referencyjnej zapisz:

- współrzędne i sposób ustalenia pozycji obserwatora;
- pochodzenie wysokości obserwatora;
- oryginalne rozmiary, obrót, crop i ewentualne prostowanie obrazu;
- parametry kamery oraz informację, które są znane, a które szacowane;
- ręcznie oznaczone punkty (peak_id,u,v) i niepewność oznaczenia;
- rozdział punktów użytych do wyrównania i punktów pozostawionych do oceny.

Na początek zbierz 3 zdjęcia z 3 stanowisk, po przynajmniej 5 oznaczonych szczytów. Użyj wybranych 1–2 punktów do ręcznej korekty heading/pitch, a pozostałych do pomiaru. Przy tak małym zbiorze raport opisuje te sceny, a nie ogólną dokładność w całych Tatrach.

Fotografia składana w panoramę cylindryczną/sferyczną wymaga innego modelu projekcji lub wycięcia poprawnego widoku perspektywicznego. Nie podawaj jej wprost do kamery pinhole.

**Metryki muszą pokazywać także pominięte przypadki.**

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

Błąd piksela to odległość euklidesowa między markerem i adnotacją tego samego id. Błąd kątowy licz między znormalizowanymi promieniami K^-1[u,v,1], z ograniczeniem iloczynu skalarnego do [-1,1] przed acos. Nie dziel po prostu pikseli przez stały przelicznik dla całego szerokiego obrazu.

Brak dopasowanych punktów daje brak metryk, nie błąd równy zero. Zdefiniuj metodę percentyla, np. nearest-rank dla p95. Dla małej liczby punktów pokaż też wszystkie błędy.

Widoczność oceniaj osobno na syntetycznych scenach i na ręcznie opisanych, jednoznacznych przypadkach. Brak szczytu na zdjęciu z chmurami nie dowodzi zasłonięcia przez teren.

Zmierz liczbę próbek widoczności i czasy p50/p95 osobno dla przygotowania danych, evaluate oraz zapisu SVG. Jako początkowy cel inżynierski przyjmij evaluate poniżej 1 s p95 dla 100 kandydatów, zakresu do 30 km i ustalonego kroku na swoim komputerze w Release. To cel do pomiaru, nie deklaracja osiągniętego wyniku ani wymaganie czasu rzeczywistego telefonu.

**MVP jest ukończone, gdy spełnia następujące warunki.**

- Rdzeń i testy syntetyczne budują się bez SDK mobilnego i bez GDAL.
- Scena syntetyczna ma dokładnie oczekiwane kierunki, projekcję i statusy widoczności.
- Pakiet PL/SK ma udokumentowane układy wysokości, źródła i pokrycie.
- Przynajmniej 30 wybranych szczytów ma stabilne id; wysokości wyświetlane są zweryfikowane albo jawnie nieznane.
- CLI renderuje scenę i serię orientacji do SVG/JSON.
- Niedostępny teren i błędne wejście nie dają pozornie prawidłowych podpisów.
- Replay jest powtarzalny w tej samej kompilacji; porównania między platformami stosują jawne tolerancje liczbowe.
- Istnieją 3 opisane sceny fotograficzne i raport błędów na punktach niewykorzystanych do wyrównania.
- Testy przechodzą w Debug/Release; ASan/UBSan nie wykazują problemów na zestawie testowym.
- Repo zawiera instrukcję odtworzenia demo, ograniczenia i pomiary na nazwanym sprzęcie.

Proponowany próg dla końcowej demonstracji fotograficznej: co najmniej 80% pozostawionych do oceny, oznaczonych punktów ma prognozę w obrazie oraz medianę błędu kątowego <=1°. To próg prototypu po ręcznym wyrównaniu, nie obietnica działania na żywo. Ustal go przed oceną, pokaż również p95 i przypadki nieudane. Jeżeli pochodzenie aparatu/pozycji jest zbyt niepewne, scena pozostaje demonstracyjna i nie zalicza tego kryterium.

Najpierw domknij przepływ wejście → geometria → wynik na sztucznych danych. Następnie dodaj model terenu, prawdziwe dane, replay i ocenę zdjęć. Implementuj po jednym zadaniu z [listy zadań](MVP_TASKS.md), zapisując przy nim wynik testu i krótki wniosek techniczny.
