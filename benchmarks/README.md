# T19: benchmarki

Wyniki IMG_9553 i decyzję o optymalizacji opisuje
[raport pomiarów](../docs/T19_PERFORMANCE.md). Runner mierzy rzeczywiste funkcje
`project`, `sampleHeight`, `traceVisibility` i `PeakEngine::evaluate` przez
`std::chrono::steady_clock`. Nie dodaje zależności Google Benchmark.

## Budowanie i próba bez danych Tatr

Wymagania jak dla adapterów desktopowych: CMake, kompilator C++20, Ninja i GDAL.
Python 3 jest potrzebny tylko do porównywania raportów i testów ich matematyki.

```sh
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel 4
ctest --preset desktop-release
./build/desktop-release/benchmarks/tatra-benchmark \
  --synthetic --out out/t19/synthetic
```

`TATRA_BUILD_BENCHMARKS` jest domyślnie OFF. Preset `desktop-release` włącza
benchmarki i adaptery; zwykły `release` nadal buduje rdzeń bez GDAL i JSON.
Przy pierwszej konfiguracji CMake pobiera przypięte zależności (albo korzysta
z podanych lokalnych katalogów `FETCHCONTENT_SOURCE_DIR_*`). Wykonanie benchmarku
nie używa sieci. Do porównań wydajności używaj **Release**, nie Debug/sanitizerów.

## IMG_9553

Benchmark wykorzystuje skalibrowaną scenę z fotografii, nie dekoduje JPEG-a.
Pozycja, orientacja i intrinsics są w
[img_9553-scene.json](fixtures/img_9553-scene.json). Nie potrzeba prywatnego zdjęcia.
Jeżeli pakiet z wcześniejszej próby istnieje:

```sh
./build/desktop-release/benchmarks/tatra-benchmark \
  --dataset out/t18/IMG_9553/dataset-height-corrected \
  --scene benchmarks/fixtures/img_9553-scene.json \
  --out out/t19/current --batches 31 --iterations 20 --warmup 3
```

Na Linuxie do pomiarów można poprzedzić polecenie `taskset -c 2` (wybierz dostępny
rdzeń; CPU 2 użyto w zapisanym raporcie). Nie uruchamiaj równolegle builda ani testów.

### Odtworzenie pakietu wejściowego

Najpierw przygotuj dane według [T10](../docs/T10_DATA.md),
[T11](../docs/T11_DEM.md) i [T13](../docs/T13_DATASET.md).
Zachowaj wskazany snapshot OSM, DEM i `download-metadata.json`; nowe pobranie OSM
może zmienić wyniki. Polecenia poniżej kieruj do nowego katalogu wyjściowego:

```sh
mkdir -p out/t19/reproduced-dataset
cp data/tatras-v1/terrain-ellipsoid.tif out/t19/reproduced-dataset/
./build/desktop-release/tatra-prepare peaks \
  --raw-osm data/raw/osm/tatras-peaks.json \
  --terrain out/t19/reproduced-dataset/terrain-ellipsoid.tif \
  --selection benchmarks/fixtures/img_9553/selection.json \
  --corrections benchmarks/fixtures/img_9553/height-corrections.json \
  --out out/t19/reproduced-dataset
./build/desktop-release/tatra-prepare manifest \
  --dataset-id morskie-oko-height-trial \
  --terrain out/t19/reproduced-dataset/terrain-ellipsoid.tif \
  --peaks out/t19/reproduced-dataset/peaks.json \
  --source-metadata data/raw/download-metadata.json \
  --attribution data/tatras-v1/ATTRIBUTION.txt \
  --out out/t19/reproduced-dataset
sha256sum out/t19/reproduced-dataset/manifest.json
```

Oczekiwany SHA-256: `235eb73eb33483d1ac2a6c8163a327c44cdcd26d0a5293d5b1b47f679607f4c4`.
Następnie podaj `--dataset out/t19/reproduced-dataset`.
Pięć korekt wysokości jest eksperymentem z OSM + N, a nie pomiarem geodezyjnym;
założenia są zapisane w fixture i [opisie próby](../docs/T18_HEIGHT_TRIAL.md).

## Porównanie dwóch wersji

Zachowaj raport i plik wykonywalny referencji **przed** zmianą algorytmu. Obie wersje
buduj tym samym kompilatorem, flagami i konfiguracją. Nową wersję uruchom z dodatkiem:

```sh
# Do polecenia benchmarku dodaj:
# --reference out/t19/before/report.json --out out/t19/after
python3 benchmarks/compare.py \
  out/t19/before/report.json out/t19/after/report.json \
  --out out/t19/comparison.json
```

`--reference` porównuje pełne wartości liczbowe i stany, bez tolerancji:
projekcje, 4096 próbek terenu, promienie, wynik kadru i dziewięć orientacji.
Inny dataset, scena lub wynik powoduje błąd zamiast zapisu pomiaru jako zgodnego.
To test równoważności na danych benchmarku, nie matematyczny dowód dla wszystkich wejść.

Dla serii naprzemiennych uruchomień powtarzaj opcję skryptu
`--extra-pair out/t19/repeat-2-before/report.json out/t19/repeat-2-after/report.json`
(analogicznie dla kolejnych par). Skrypt sprawdza również zgodność konfiguracji
powtórzeń, środowiska i buildów między kolejnymi przebiegami każdej wersji.
Wynik zbiorczy to mediana median przebiegów; p95 pochodzi ze wszystkich średnich
partii. **Nie jest to p95 opóźnienia pojedynczego wywołania.**

## Zakres pomiaru i ograniczenia

- Pliki i walidacja pakietu: osobne `load_files_ms`; to nie test zimnego dysku.
- Przygotowanie ENU/list współrzędnych i kontrola wyników: poza zegarem pomiaru.
- Rozgrzewka: osobne `warmup_ms` dla każdej funkcji, domyślnie 3 partie.
- Analiza: domyślnie 31 partii; liczba iteracji i operacji jest jawna w JSON-ie.
- Zapis JSON i ewentualne tworzenie SVG: poza analizą; runner nie zapisuje SVG.
- `evaluate` obejmuje jego walidację, alokacje, projekcję, widoczność i sortowanie.
- Zbiory są stałe, w ustalonej kolejności; mierzymy rozgrzany dostęp, nie losowy
  odczyt całego rastra. Narzut pętli i sumowania wyników jest wliczony; zapis sumy
  do `volatile`, zabezpieczający przed usunięciem pracy, jest poza sekcją mierzoną.
- Pamięć: `sizeof(GridTerrain)`, payload rastra i szczytowy RSS procesu do końca analizy
  (Linux), nie liczba alokacji pojedynczego `evaluate`. RSS obejmuje też GDAL,
  fixture syntetyczne i kontrolne JSON-y. Na innych systemach uzupełnij CPU/RAM/OS
  ręcznie; niedostępny RSS jest `null`.
- Znacznik `configured_git_revision` to HEAD podczas konfiguracji, a nie skrót
  niezatwierdzonych zmian. Raport T19 dodatkowo opisuje dokładną zmianę referencji.
- Bez progów czasowych w CTest: testy sprawdzają poprawność, nie szybkość maszyny CI.
