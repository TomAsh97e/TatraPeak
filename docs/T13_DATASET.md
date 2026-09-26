# T13: walidowany pakiet regionu

`tatra-prepare manifest` składa wyniki T10–T12 w jeden pakiet, zapisuje
deterministyczny `manifest.json`, kopiuje atrybucję i przed zakończeniem ponownie
ładuje cały pakiet przez ten sam loader, którego używa aplikacja:

```sh
./build/desktop-debug/tatra-prepare manifest \
  --dataset-id tatras-v1 \
  --terrain data/tatras-v1/terrain-ellipsoid.tif \
  --peaks data/tatras-v1/peaks.json \
  --source-metadata data/raw/download-metadata.json \
  --attribution data/raw/ATTRIBUTION.txt \
  --out data/tatras-v1
```

Gotowy katalog zawiera dokładnie pliki używane w czasie działania:

- `manifest.json` — wersję schematu, sumy i rozmiary plików, geometrię rastra,
  układy odniesienia, pochodzenie konwersji wysokości i źródła danych;
- `terrain-ellipsoid.tif` — raster T11;
- `peaks.json` — katalog T12;
- `ATTRIBUTION.txt` — wymagane informacje o źródłach i licencjach.

Plik `download-metadata.json` pozostaje wejściem przygotowania. Manifest zachowuje
jego SHA-256, datę pozyskania, zakresy oraz wpisy źródeł, ale nie wymaga tego pliku
w pakiecie uruchomieniowym.

## Kontrola pakietu

Podsumowanie można wypisać poleceniem:

```sh
./build/desktop-debug/tatra dataset-info --dataset data/tatras-v1
```

Polecenie najpierw wykonuje pełne `loadDataset` i niczego nie wypisuje jako
poprawnego, dopóki walidacja się nie zakończy. Sprawdzane są między innymi:

- wersja i ścisła struktura manifestu oraz bezpieczne, lokalne nazwy plików;
- obecność, rozmiar i SHA-256 terenu, katalogu i atrybucji;
- zgodność wymiarów, geotransformacji, pokrycia, NoData i `AREA_OR_POINT` rastra;
- `EPSG:4326`, wynik `WGS84 ellipsoid`, źródłowy EGM2008 oraz pełne pochodzenie
  konwersji `h = H + N`, włącznie z modelem i SHA-256 geoidy;
- zgodność źródłowych plików DEM i ich sum z metadanymi zapisanymi w GeoTIFF;
- liczbę szczytów, unikalność stabilnych identyfikatorów, pokrycie przez DEM i
  zgodność każdej wysokości geometrycznej z próbką terenu;
- obecność wymaganych informacji o Copernicus DEM, GeographicLib i OpenStreetMap.

Sama nazwa `terrain-ellipsoid.tif` ani tekstowa etykieta układu pionowego nie są
dowodem normalizacji. Loader wymaga zgodnych metadanych i pochodzenia z T11.
Obiekty `GridTerrain`, `PeakCatalog` i `DatasetMetadata` powstają jako własność
jednego `Dataset`; silnik może bezpiecznie przechowywać referencje dopiero po
zakończeniu ładowania i walidacji.

## Wynik dla `tatras-v1`

Pakiet zawiera raster 4321 × 2160 i 50 szczytów. Dla bieżących wejść jego
`manifest.json` ma SHA-256:
`2206037d24a47a1e5d825232a61d487d157f497e1ef339daefbd924a7ad75775`.

Test integracyjny tworzy mały pakiet 2 × 2 całkowicie lokalnie. Obejmuje poprawne
ładowanie oraz odrzucanie złej wersji, brakującego pliku, błędnej sumy, niezgodnych
wymiarów i geotransformacji, złego CRS lub pochodzenia wysokości, powtórzonego id i
szczytu poza pokryciem.
