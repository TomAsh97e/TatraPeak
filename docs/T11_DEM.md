# T11: normalizacja DEM

Narzędzie `tatra-prepare dem` przyjmuje wiele kafli przez powtarzalne `--raw-dem`,
mozaikuje ich wspólną naturalną siatkę, przycina ją do jawnego bbox i oblicza dla
każdej poprawnej próbki `h = H + N`. Oryginalne pliki nie są modyfikowane.

Przygotowanie roboczego rastra Tatr:

```sh
./build/desktop-debug/tatra-prepare dem \
  --raw-dem data/raw/copernicus-dem/Copernicus_DSM_COG_10_N49_00_E019_00_DEM.tif \
  --raw-dem data/raw/copernicus-dem/Copernicus_DSM_COG_10_N49_00_E020_00_DEM.tif \
  --geoid-dir data/raw/geographiclib/geoids \
  --geoid-model egm2008-2_5 \
  --bounds 19.4 49.0 20.6 49.6 \
  --out data/tatras-v1
```

Wynik `terrain-ellipsoid.tif` ma 4321×2160 próbek, krok 1/3600°, EPSG:4326,
Float32 i `AREA_OR_POINT=Point`. Zakres brzegów według geotransformacji to
19.3998611111–20.6001388889°E oraz 49.0001388889–49.6001388889°N. Pierwszy i
ostatni środek piksela mieszczą się na naturalnej siatce źródłowej; nie wykonano
zwiększenia rozdzielczości.

Maska jest zapisana wewnątrz GeoTIFF-u jako `GMF_PER_DATASET`. Brak deklaracji
NoData w dwóch wejściowych kaflach pozostaje jawną cechą danych, a wysokość zero
nie jest interpretowana jako brak próbki.

## Metadane pochodzenia

Wynik zapisuje:

- `TATRA_VERTICAL_REFERENCE=WGS84 ellipsoid`;
- `TATRA_SOURCE_VERTICAL_REFERENCE=EGM2008 (EPSG:3855)`;
- `TATRA_GEOID_MODEL=egm2008-2_5`;
- `TATRA_GEOID_SHA256=fab040a55dfabe782be89a89b2ba7e4a73183513a9813e24a3f80e7b6ed61dbf`;
- `TATRA_NORMALIZATION_FORMULA=h = H + N`;
- nazwy i SHA-256 obu źródłowych kafli DEM.

`loadNormalizedGeoTiff` odrzuca raster bez powyższego jawnego potwierdzenia
wysokości elipsoidalnych. Samo przypisanie nowej etykiety pionowego układu bez
zmiany wartości nie tworzy poprawnego wejścia.

## Kontrola GeographicLib

Referencyjne wartości uzyskano niezależnym programem `GeoidEval` z GeographicLib
2.7 i tym samym plikiem `egm2008-2_5.pgm`:

| Latitude | Longitude | H z GLO-30 | h w wyniku | N z różnicy | N z GeoidEval |
|---:|---:|---:|---:|---:|---:|
| 49.6000 | 19.4000 | 745.7519 | 787.6763 | 41.9244 | 41.9244 |
| 49.3000 | 20.0000 | 828.7858 | 870.8978 | 42.1120 | 42.1120 |
| 49.1000 | 20.4000 | 644.8524 | 686.5072 | 41.6548 | 41.6548 |

SHA-256 utworzonego lokalnie GeoTIFF-u w tym przebiegu:
`943edd67c853bc1b8c5074e7c9b18c82620182f3111944419da11ec61c3c2a0c`.
Plik pozostaje w `data/`, które jest celowo ignorowane przez Git.
