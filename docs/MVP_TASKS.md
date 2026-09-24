**TatraPeak: zadania implementacyjne MVP.**

Lista odpowiada kontraktom ze [specyfikacji architektury](MVP_PLAN.md). Kod i komendy są planem do wykonania. Każde zadanie kończy się konkretnym wynikiem, testem i krótką notatką o podjętej decyzji. W pierwszym przebiegu wybieraj prostą implementację referencyjną; optymalizację wykonaj po pomiarach.

| ID | Efekt | Zależności |
|---|---|---|
| T01 | Budowanie biblioteki i testów | — |
| T02 | Typy domenowe i walidacja | T01 |
| T03 | WGS84/ECEF/ENU | T02 |
| T04 | Obrót kamery i projekcja | T02 |
| T05 | Siatka terenu i interpolacja | T02 |
| T06 | Ocena przesłaniania | T03, T05 |
| T07 | Kompletny silnik dla danych w pamięci | T03, T04, T06 |
| T08 | Odczyt sceny i eksport JSON | T07 |
| T09 | Wizualizacja SVG | T08 |
| T10 | Zarchiwizowane surowe dane Tatr | —; najwygodniej po T09 |
| T11 | DEM o wysokościach elipsoidalnych | T05, T10 |
| T12 | Katalog szczytów | T10, T11 |
| T13 | Walidowany pakiet regionu | T08, T11, T12 |
| T14 | Pierwsza panorama geometryczna Tatr | T09, T13 |
| T15 | Oś czasu orientacji | T04 |
| T16 | Replay sesji | T08, T09, T14, T15 |
| T17 | Metryki błędu projekcji | T04, T08 |
| T18 | Zestaw zdjęć i raport jakości | T14, T17 |
| T19 | Pomiary i jedna uzasadniona optymalizacja | T16 |
| T20 | Reprodukowalne demo i kontrola jakości | T18, T19 |

T01–T09 tworzą pierwszy zamknięty etap, działający całkowicie na sztucznych danych. T10–T14 zastępują je prawdziwym pakietem. T15–T20 zapewniają powtarzalność, ocenę i prezentację.

**T01 przygotowuje budowanie projektu.**

Praca:

- Utwórz tatra_core jako zwykłą bibliotekę C++20, początkowo statyczną.
- Dodaj target tatra_tests i rejestrację przez CTest.
- Dodaj Eigen, GeographicLib i GoogleTest z przypiętymi wersjami; wybierz jeden sposób zarządzania zależnościami.
- Dodaj presety Debug, Release i Debug z ASan/UBSan dla wspieranego kompilatora.
- Dodaj ostrzeżenia kompilatora dla własnych targetów, bez narzucania ich obcym bibliotekom.
- Wprowadź TATRA_BUILD_DESKTOP_IO, początkowo OFF.

Test odbiorowy: czysty build i jedno rzeczywiste sprawdzenie prostej funkcji, np. degreesToRadians(180)=π. CTest uruchamia test bez telefonu, internetu w czasie testu i danych Tatr.

Uczysz się: targetów CMake, PUBLIC/PRIVATE zależności, konfiguracji kompilatora i oddzielenia kodu od środowiska.

**T02 ustala typy i błędy wejścia.**

Pliki: types.hpp, validation.hpp/.cpp.

Funkcje do napisania:

~~~cpp
Result<GeoCoord> makeGeoCoord(double latitude_deg, double longitude_deg);
Result<GeoPosition> makeGeoPosition(GeoCoord, double ellipsoid_height_m);
Result<CameraIntrinsics> validateCamera(CameraIntrinsics);
Result<CameraPose> validatePose(CameraPose);
Result<QueryOptions> validateOptions(QueryOptions);
~~~

Nie wykorzystuj wysokości nad poziomem morza jako domyślnego synonimu wysokości elipsoidalnej. Dla GeoPosition dokumentuj wyłącznie wysokość elipsoidalną.

Testy:

- lat=-90/90 i lon=-180/180 przechodzą; wartości poza zakresem nie.
- NaN i infinity są odrzucane.
- Zerowe rozmiary obrazu, fx<=0, fy<=0 są błędem.
- Kwaternion o normie zero jest błędem; zdefiniowane niewielkie odchylenie od normy jest normalizowane.
- Krok próbkowania<=0, max_samples=0, ujemne guardy i min_range>=max_range są błędem.
- Punkt główny kamery nie musi leżeć idealnie w środku; nie narzucaj tego bez potrzeby.

Gotowe, gdy każdy błąd ma ErrorCode i zrozumiałą przyczynę. Uczysz się: rozdzielenia stanu poprawnego, braku danych i błędu, enum class, variant/optional oraz granic walidacji.

**T03 tworzy geometrię świata.**

Pliki: geo.hpp/.cpp. Funkcje: toEcef, fromEcef, LocalFrame::toEnu/fromEnu, bearingElevation.

Owiń GeographicLib we własne małe typy. Nie przepisuj geodezji produkcyjnej od zera; samodzielnie wyprowadź i zapisz znaczenie osi oraz wzór na azymut/elewację.

Testy:

- Znany punkt WGS84 (0°,0°,0 m) daje (6378137,0,0) w ECEF.
- Punkt będący początkiem LocalFrame daje ENU bliskie zeru.
- Dla początku na równiku i lon=0 sprawdź niezależnie mapowanie przesunięć ECEF na osie ENU.
- bearingElevation dla ENU (0,1000,0) daje północ i 0° elewacji; dla (1000,0,0) wschód; dla (0,1000,1000) elewację 45°.
- Wektor zerowy daje Error.
- Round-trip kilku punktów ma tolerancję np. 1 mm w przestrzeni; nie zastępuje powyższych niezależnych przypadków.
- Dokładnie pionowy kierunek wymaga udokumentowanej wartości azymutu, np. 0° jako umownej; elewacja wynosi +/-90°.

Gotowe, gdy potrafisz objaśnić różnicę między wysokością, odległością przestrzenną i dystansem po powierzchni. Uczysz się: transformacji, precyzji double i projektowania testów numerycznych.

**T04 rzutuje punkt na obraz.**

Pliki: camera.hpp/.cpp. Funkcje: cameraFromHorizontalFov, poseFromHeadingPitchRoll, project.

Zbuduj bazę kamery według specyfikacji, następnie zamień ją na kwaternion. Oddziel tworzenie parametrów kamery od samej projekcji.

Testy:

- Domyślna kamera patrzy na północ.
- Przy fx=fy=1000, cx=640, cy=360 punkt kamery (0,0,1000) daje (640,360).
- Punkt (100,0,1000) daje u=740; punkt (0,100,1000) daje v=460.
- W ENU punkt wyżej niż obserwator pojawia się wyżej na ekranie.
- Heading=90° kieruje kamerę na wschód.
- Roll=90° działa zgodnie z opisaną bazą; test kontroluje oba kierunki, nie samą normę kwaternionu.
- Z<=near_z daje BehindNearPlane bez NaN.
- Punkt za granicą obrazu daje OutsideFrame, ale zachowuje obliczone u,v.
- Punkty dokładnie na prawym/dolnym brzegu są poza obrazem.
- HFOV<=0 lub >=180° jest błędem.

Gotowe, gdy wszystkie oczekiwane piksele pochodzą z ręcznych obliczeń, nie z drugiego wywołania project. Uczysz się: modelu pinhole, konwencji obrotu, clippingu i kwaternionów.

**T05 odczytuje wysokość ze sztucznego rastra.**

Pliki: terrain.hpp/.cpp. Funkcje: GridTerrain::create, sampleHeight.

Praca:

- Przechowuj wartości jako float w ciągłym vector, obliczenia indeksów/interpolacji wykonuj w double.
- Waliduj wymiary >=2, skończone współrzędne i zgodność liczby elementów.
- Sprawdzaj przepełnienie rows*columns przed alokacją lub porównaniem rozmiaru.
- Dodaj maskę poprawności. Zera i ujemne wysokości pozostają prawidłowymi danymi.
- Zdefiniuj zachowanie na ostatnim środku piksela: jest dozwolony. Nie „przyklejaj” współrzędnych spoza rastra do jego brzegu.
- Przy interpolacji wymagaj poprawności tylko tych próbek, których waga jest niezerowa.

Testy:

- Siatka 100,120 / 140,160 daje w środku 130.
- Każdy narożny środek piksela zwraca własną wartość.
- Ujemny krok szerokości geograficznej mapuje północ na początek tablicy.
- OutsideCoverage i NoData są rozróżniane.
- Wszystkie próbki mające niezerową wagę są uwzględnione.
- Niepoprawny rozmiar/maska/krok daje błąd konstrukcji.
- Brzeg rastra i skrajne indeksy przechodzą ASan.

Gotowe, gdy testy nie potrzebują GDAL. Uczysz się: row-major, interpolacji, zakresów indeksów, reprezentacji brakujących danych i własności pamięci.

**T06 sprawdza przesłanianie terenem.**

Pliki: visibility.hpp/.cpp. Funkcja: traceVisibility.

W testach napisz małe implementacje ITerrain: stała wysokość elipsoidalna, podwyższony pas grani na określonym obszarze lat/lon, brakujące dane w wybranym pasie. Syntetyczny obserwator i cel mogą być wysoko nad powierzchnią, aby oczekiwany wynik nie zależał od subtelnej krzywizny.

Testy:

- Teren daleko pod promieniem: VisibleInModel.
- Jednoznacznie wysoka grań na środku odcinka: OccludedInModel.
- Wysoki teren za szczytem nie wpływa na wynik.
- Brak danych na części niezasłoniętej ścieżki: MissingTerrain.
- Znana przeszkoda wystarcza do OccludedInModel, nawet jeśli wcześniej brakowało danych.
- Próbka blisko promienia, w paśmie tolerancji: Uncertain.
- Obserwator lub cel niezgodny z powierzchnią modelu: zgodnie z kontraktem Uncertain.
- Zbyt krótki odcinek albo przekroczony budżet próbek: Uncertain z konkretną przyczyną.
- Dla znanej przeszkody first_blocker_distance mieści się w jej przedziale z tolerancją kroku.
- Na szerokiej grani kroki 10/5/2.5 m zgadzają się. Osobny test wąskiej grani pokazuje ograniczenie próbkowania.

Gotowe, gdy można wyświetlić profil próbek: dystans, wysokość promienia, wysokość terenu, clearance. Uczysz się: algorytmu referencyjnego, kosztu próbkowania, wczesnego wyjścia i uczciwego modelowania niepewności.

**T07 składa pierwszy kompletny silnik w pamięci.**

Pliki: catalog.hpp, engine.hpp/.cpp. Funkcje: PeakEngine::evaluate oraz niewielkie pomocnicze funkcje walidacji.

Praca:

- Przyjmij teren i katalog przez const referencje z jasno opisanym czasem życia.
- Zaimplementuj filtrowanie zakresu, kierunek, projekcję, widoczność.
- Widoczność licz tylko dla InFrame.
- Zwracaj wyniki stabilnie posortowane po id.
- Zliczaj kandydatów odrzuconych przez zakres i liczbę próbek widoczności.

Scena integracyjna zawiera: widoczny szczyt na wprost, szczyt poza obrazem, szczyt za kamerą, szczyt za granią oraz szczyt z brakującym terenem.

Test odbiorowy: każdy rekord ma oczekiwany status; widoczność dla punktów spoza kadru nie jest liczona. Instrumentowana implementacja ITerrain liczy wywołania i potwierdza tę właściwość.

Gotowe, gdy jeden test przechodzi przez cały pipeline bez plików. Uczysz się: kompozycji, const-correctness, lifetime i rozdzielenia algorytmu od orkiestracji.

**T08 dodaje wejście sceny i czytelny wynik JSON.**

Pliki: adapters/desktop/scene_loader.cpp, result_writer.cpp, apps/tatra/main.cpp.

Funkcje: loadScene, writeFrameJson. Na tym etapie można używać małego katalogu i terenu testowego wbudowanego w demo; docelowy loadDataset dochodzi w T13.

Praca:

- Dodaj schema_version=1.
- Obsłuż kamerę opisaną intrinsics albo HFOV; odrzuć równoczesne sprzeczne warianty.
- Obsłuż wysokość obserwatora ellipsoid albo above_dem; ten drugi wariant wymaga poprawnej próbki terenu.
- Przelicz heading/pitch/roll na CameraPose.
- Waliduj typy JSON bez cichego zastępowania błędnych pól zerem.
- Ustal kody wyjścia: 0 sukces, 2 błędne wejście, 3 błąd I/O; ostrzeżenie o częściowym terenie zapisuj w wyniku.

Testy: brak pliku, niepoprawny JSON, zła wersja, brak wymaganych pól, niefinitywne/niepoprawne liczby po konwersji, nieobsługiwany wariant. Round-trip danych uzupełnij sprawdzeniem ręcznie napisanego pliku referencyjnego.

Gotowe, gdy CLI przetwarza scenę syntetyczną i zapisuje zrozumiały wynik. Uczysz się: projektowania schematu, błędów na granicy systemu i adapterów.

**T09 przedstawia wynik jako SVG.**

Plik: svg_writer.cpp. Funkcje: escapeXml, layoutLabels, writeOverlaySvg.

Praca:

- Ustaw viewBox zgodny z wymiarami kamery.
- Rysuj marker dokładnie w punkcie projekcji.
- Rysuj etykietę z nazwą i wysokością katalogową albo informacją o braku wysokości.
- Wprowadź odróżnienie statusów, legendę i tryb debug.
- Dla kolizji tekstu zastosuj prostą deterministyczną strategię; marker pozostaje nieruchomy.
- Opcjonalne tło musi mieć ten sam układ pikseli. Nie stosuj ukrytego cropu lub niezależnego skalowania zdjęcia.

Testy: nazwa z &, <, > i polskimi znakami; dwie kolidujące etykiety; marker blisko brzegu; odróżnienie MissingTerrain od Occluded. Sprawdź również wygenerowany obraz w przeglądarce.

Gotowe, gdy można otworzyć wynik syntetycznego renderu bez dodatkowego GUI. Uczysz się: odseparowania prezentacji, poprawnej serializacji i różnicy między markerem a etykietą.

**T10 gromadzi surowe dane bez pisania silnika pobierającego.**

Wykonaj ręcznie jednorazowe pozyskanie danych według [specyfikacji](MVP_PLAN.md):

- Kafle GLO-30 pokrywające roboczy prostokąt 19.4–20.6°E, 49.0–49.6°N.
- Model geoidy egm2008-2_5.
- Eksport węzłów natural=peak przez Overpass, obejmujący obie strony Tatr.
- Metadane pobrania i SHA-256 każdego pliku.

Test odbiorowy: GDAL pokazuje oczekiwany CRS, rozmiar, NoData i zakres współrzędnych rastra; surowy eksport ma id, lat/lon i tagi. Obejrzyj granicę regionu oraz kilka znanych nazw. Żaden z plików nie jest tylko zrzutem ekranu ani obrazem cieniowania terenu.

Gotowe, gdy późniejsze kroki mogą działać bez sieci. Uczysz się: pochodzenia danych, formatów GIS i reprodukowalności.

**T11 normalizuje DEM do wspólnej wysokości.**

Pliki: geotiff_loader.cpp, narzędzie prepare.

Funkcje:

~~~cpp
readDemGeoTiff(...);                    // dane + surowe metadane
pixelCenterGeoCoord(...);              // row/column -> lat/lon
normalizeHeightGridToEllipsoid(...);    // h = H + N
writeNormalizedGeoTiff(...);
~~~

Praca:

- Przytnij/mozaikuj dane narzędziami GDAL; zachowaj naturalną siatkę bez sztucznego zwiększania rozdzielczości.
- Opakuj uchwyt GDALDataset w RAII, zamykając go przez GDALClose.
- Odczytaj NoData i maskę; wyzerowany raster nie oznacza braku terenu.
- Korzystaj z geotransformacji GDAL i środków pikseli.
- Dodaj geoidę dla każdego poprawnego piksela. Gdy model geoidy jest niedostępny, zakończ z błędem.
- Zachowaj oryginalne dane i zapisz poprawną informację o wynikowych wysokościach w metadanych.

Testy:

- Mały raster z ręcznie znaną geotransformacją: poprawne środki pikseli.
- Sztuczna funkcja geoidy N=40 m daje 1000 m -> 1040 m.
- Maska NoData przetrwa konwersję.
- Odczyt-zapis nie zamienia wierszy ani kolumn.
- Kilka rzeczywistych wartości porównaj z narzędziem GeographicLib GeoidEval; to kontrola integracji, nie niezależna walidacja modelu geoidy.
- Błędny/nieobsługiwany CRS jest odrzucany.

Gotowe, gdy cały wynik ma wysokości elipsoidalne i pełną historię przeliczenia. Uczysz się: RAII dla C API, raster I/O i różnicy między zmianą etykiety CRS a zmianą danych.

**T12 tworzy katalog szczytów.**

Pliki: narzędzie prepare, importer OSM.

Funkcje: parseOsmPeakNodes, validateAndDeduplicatePeaks, assignGeometryHeights.

Praca:

- Utwórz stabilne id z typu i identyfikatora OSM.
- Ustal kolejność nazw, np. name:pl, następnie name, następnie name:sk; zachowaj nazwy źródłowe w pliku danych.
- Parsuj ele ostrożnie, odrzucając niejednoznaczne wartości do raportu. Nie wpisuj zera za brak.
- Nie deduplikuj po nazwie lub zaokrąglonych współrzędnych; scalać można ten sam OSM id.
- Pobierz wysokość geometryczną z przygotowanego GridTerrain.
- Wybierz i ręcznie skontroluj 30–100 szczytów. Poprawki przechowuj w osobnym, udokumentowanym pliku.

Testy: polskie/słowackie znaki, brak nazwy lokalnej, brak ele, błędne ele, powtórzony id, dwie różne góry o tej samej nazwie, punkt poza DEM. Import ma raport liczby przyjętych/odrzuconych rekordów i przyczyn.

Gotowe, gdy wysokość do etykiety i wysokość do geometrii nie mogą zostać przypadkowo utożsamione. Uczysz się: ETL, identyfikatorów, jakości danych i rozdzielania modeli domenowych.

**T13 ładuje i waliduje pakiet regionu.**

Pliki: dataset_loader.cpp, manifest parser. Funkcje: loadDataset, validateDataset, writeDatasetManifest.

Dataset posiada GridTerrain, PeakCatalog i DatasetMetadata. App tworzy silnik dopiero po zakończeniu odczytu i walidacji, aby jego referencje nie zostały unieważnione podczas przenoszenia danych.

Testy:

- Zgodne pliki tworzą Dataset.
- Nieobsługiwana wersja, brak pliku, zły hash, niezgodny CRS/wysokości dają błąd.
- Wymiary i geotransformacja niezgodne z manifestem są odrzucane.
- Powtórzone id lub szczyt poza pokryciem wykrywa walidacja.
- Mała syntetyczna paczka testuje loader bez pobierania danych z internetu.
- Wysokość w EGM2008 opisana jako elipsoidalna nie może zostać zaakceptowana tylko na podstawie nazwy pliku; wymagaj zgodnych metadanych i pochodzenia przygotowania.

Gotowe, gdy CLI ładuje pakiet i potrafi wypisać jego podsumowanie. Uczysz się: niezmienników wielu plików, własności obiektów i spójności danych.

**T14 uruchamia pierwszą rzeczywistą scenę Tatr.**

Praca:

- Wybierz wirtualną pozycję w obrębie pakietu; zapisz ją jawnie jako symulowaną.
- Wygeneruj kilka widoków z różnymi heading/pitch, tym samym obserwatorem i kamerą.
- Sprawdź, czy nazwy przesuwają się w oczekiwanym kierunku i pojawiają tylko w odpowiednich sektorach.
- W trybie debug wyeksportuj próbki linii widzenia dla kilku szczytów.
- Obejrzyj miejsce styku kafli i przypadek poza pokryciem.

Test odbiorowy: wynik można odtworzyć pojedynczym poleceniem po pobraniu tego samego pakietu; zawiera dataset_id/hash. Błąd pokrycia nie daje VisibleInModel.

Gotowe, gdy masz pierwsze demo na rzeczywistym terenie. Uczysz się: diagnozowania całego pipeline i różnic między poprawnością wzorów a jakością danych.

**T15 interpoluje orientację na osi czasu.**

Pliki: pose_timeline.hpp/.cpp. Funkcje: PoseTimeline::create, sampleAt.

Praca:

- Przechowuj próbki w kolejności rosnącego czasu.
- Szukaj sąsiadów przez lower_bound.
- Dla dokładnie zgodnego czasu zwracaj próbkę.
- W pozostałych przypadkach policz udział czasu w double i wykonaj SLERP najkrótszą drogą.
- Przyjmij jawną zasadę: brak ekstrapolacji i limit odstępu między próbkami.

Testy: 359°→1°, q i -q opisujące ten sam obrót, połowa obrotu 0°→90°, dokładny timestamp, pojedyncza próbka, pusty zbiór, duplikat czasu, czas poza zakresem, zbyt duża luka, zerowy kwaternion.

Gotowe, gdy zmiana częstotliwości próbek nie zmienia znanej trajektorii poza tolerancją interpolacji. Uczysz się: chrono, wyszukiwania binarnego, reprezentacji obrotów i kontraktów czasowych.

**T16 odtwarza sesję bez telefonu.**

Pliki: session_loader.cpp, obsługa polecenia replay.

Praca:

- Obsłuż plik sesji ze stałym obserwatorem i kamerą, próbkami orientacji oraz czasami klatek.
- Zdefiniuj format kwaternionu w JSON jako nazwane pola w,x,y,z; nie polegaj na kolejności pamięci biblioteki.
- Dla każdej klatki wywołaj sampleAt, zbuduj FrameQuery, użyj evaluate.
- Zapisuj wyniki według indeksu klatki i jej timestampu.
- Rejestruj błędy klatek, liczbę pominiętych i metadane sesji.
- Dodaj generator kontrolowanej sesji obrotu oraz wariant z zadanym błędem heading lub opóźnieniem próbek.

Testy:

- Dwukrotne wykonanie tej samej sesji tą samą kompilacją daje te same wyniki merytoryczne; czas wykonania jest osobną metryką.
- Dodane opóźnienie zmienia projektowane punkty w przewidywalnym kierunku.
- Brakujące próbki prowadzą do jawnego błędu, bez używania ostatniej dowolnej orientacji.
- Zmiana szybkości odtwarzania nie wpływa na wynik, bo replay nie czyta zegara ściennego.

Gotowe, gdy błąd można zgłosić, dołączając małą sesję i polecenie uruchomienia. Uczysz się: reprodukcji usterek, deterministyczności i oddzielenia czasu danych od czasu wykonania.

**T17 liczy rzeczywisty błąd projekcji.**

Pliki: tools/evaluation/reprojection_metrics.cpp.

Funkcje: pixelError, pixelToUnitRay, angularError, nearestRankPercentile, measureReprojection.

Testy:

- Różnica (3,4) piksele daje błąd 5 px.
- Identyczne promienie dają 0°.
- Ręcznie znane dwa promienie dają właściwy kąt.
- Wartość iloczynu skalarnego nieznacznie poza [-1,1] nie daje NaN.
- Dla listy błędów 1,2,3,4,5 mediana to 3, a p95 nearest-rank to 5.
- Brak dopasowanych id daje null metryk oraz odpowiednią liczbę missing.
- Wyniki spoza obrazu i ukryte etykiety nie znikają po cichu z mianownika; raport pokazuje liczbę prognoz w obrazie, braków i ocenionych punktów.

Gotowe, gdy raport umożliwia odróżnienie „mały błąd” od „algorytm zwrócił tylko jeden łatwy punkt”. Uczysz się: metryk, błędów selekcji i porównywalnej ewaluacji.

**T18 tworzy niewielki, wiarygodny zestaw fotograficzny.**

Praca:

- Znajdź 3 zdjęcia z 3 wiarygodnie ustalonych punktów obserwacji.
- Zapisz źródło, autora, warunki użycia oraz jakość danych lokalizacji.
- Ustal rozmiar/crop/orientację zdjęcia i parametry kamery; jawnie zaznacz wartości szacowane.
- Oznacz co najmniej 5 wierzchołków na zdjęcie.
- Przed wyrównywaniem wybierz 1–2 punkty kalibracyjne. Pozostałe zachowaj do oceny.
- Ręcznie skoryguj heading/pitch w scenie. Gdy f/roll też są nieznane, zapisz dodatkowe stopnie swobody i potrzebę większej liczby punktów kalibracyjnych; nie przedstawiaj tego jako testu przy znanej kamerze.
- Wygeneruj overlay i raport błędu. Zapisz również nieudane przypadki.

Test odbiorowy: końcowy raport oddziela punkty kalibracyjne i oceniające oraz podaje missing, medianę, p95 i zakres błędów. Sceny o niewiarygodnym położeniu obserwatora mają status demonstracyjny, nie walidacyjny.

Próg przyjęty w specyfikacji jest celem prototypu po ręcznej kalibracji. Jeśli go nie osiągasz, diagnozuj kolejno: osie/projekcję, intrinsics/crop, pozycję i wysokość, jakość DEM oraz identyfikację szczytu. Nie zmieniaj progu po obejrzeniu wyników bez udokumentowania powodu.

Gotowe, gdy ktoś inny może odtworzyć ocenę. Uczysz się: źródeł prawdy, oddzielania kalibracji od testu i analizy błędów systematycznych.

**T19 mierzy koszt i uzasadnia optymalizację.**

Pliki: benchmarks/, raport pomiarów.

Praca:

- Benchmark project dla ustalonego zestawu punktów.
- Benchmark sampleHeight dla stałej listy współrzędnych.
- Benchmark traceVisibility dla widocznego szczytu oraz szczytu z wczesną przeszkodą.
- Pomiar całego evaluate w Release dla jawnie określonego katalogu, kadru, zasięgu i kroku.
- Oddziel odczyt plików, rozgrzewkę i zapis SVG od samej analizy.
- Podaj CPU, RAM, OS, kompilator, flagi, wersję danych i liczbę powtórzeń.

Pierwsze możliwe optymalizacje: wcześniejsze odrzucanie poza kadrem, rezerwacja wynikowych wektorów, przeliczenie stałych pozycji szczytów na ECEF, cache widoczności przy nieruchomym obserwatorze. Klucz cache musi obejmować obserwatora, wersję danych i parametry widoczności; sama orientacja nie zmienia linii widzenia do szczytu.

Test odbiorowy: po optymalizacji wszystkie testy nadal przechodzą, wynik jest zgodny z wersją referencyjną, a raport pokazuje zmianę czasu i pamięci. Jeśli nie ma poprawy lub potrzeby optymalizacji, zachowaj prostą wersję i udokumentuj pomiar.

Nie dodawaj SIMD, własnego alokatora ani kolejki lock-free bez wykazanego problemu. Uczysz się: profilowania, benchmarków, kosztu abstrakcji i optymalizacji opartej na dowodach.

**T20 domyka wersję możliwą do pokazania.**

Praca:

- Uruchamiaj testy rdzenia na Linuxie i Windowsie; później także macOS, gdy dostępny.
- Na Linuxie dodaj ASan/UBSan; obsługę innych narzędzi uzależnij od rzeczywistej platformy.
- Testy domyślne korzystają z małych lokalnych fixture. Pełny dataset i zdjęcia mogą być osobnym zestawem integracyjnym.
- Zapewnij działanie builda rdzenia przy TATRA_BUILD_DESKTOP_IO=OFF.
- Dodaj instrukcję pobrania/przygotowania danych oraz jedno polecenie demo.
- Opisz ograniczenia: znane wejście orientacji, model pinhole, przybliżone przesłanianie, brak chmur/drzew aktualnej sceny, brak pełnej walidacji terenowej.
- Dodaj przykładowy SVG, raport jakości, raport wydajności i krótki film replay.
- Wyjaśnij decyzje: double dla geometrii, float dla rastra, Result zamiast cichego zera, const referencje, brak zależności mobilnych.

Test odbiorowy: po wykonaniu instrukcji na czystym środowisku można uruchomić demo syntetyczne bez sieci podczas wykonania, a prawdziwe demo po przygotowaniu danych. Samo pobranie zależności przy pierwszym buildzie może wymagać internetu i powinno być opisane.

Gotowe, gdy wszystkie kryteria MVP ze specyfikacji są zaliczone albo jawnie wskazane jako nieukończone. Uczysz się: dostarczania projektu, przenośności i komunikowania dowodów działania.

**Po każdym zadaniu zapisz cztery krótkie odpowiedzi.**

1. Co dokładnie jest wejściem i wynikiem tej funkcji?
2. Kto posiada dane i jak długo są poprawne?
3. Skąd wiem, że test ma niezależnie poprawny wynik?
4. Jaką informację otrzyma użytkownik funkcji, gdy założenia przestaną obowiązywać?

Dobry pierwszy zestaw pracy to T01–T04. Po nim potrafisz już zamienić położenie przestrzenne szczytu na punkt obrazu i masz testy rozstrzygające, czy używasz poprawnych osi. Dane Tatr dodajesz dopiero do sprawdzonej geometrii.
