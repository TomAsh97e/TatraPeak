# T10: surowe dane Tatr

Pobranie wykonano 2026-09-25. Duże pliki pozostają lokalnie w ignorowanym przez Git
`data/raw/`; repozytorium zawiera pochodzenie i wyniki kontroli, ale nie kopiuje około
190 MB danych źródłowych.

## Źródła

- Copernicus DEM GLO-30 Public, wydanie 2021 z publicznego bucketu AWS:
  `Copernicus_DSM_COG_10_N49_00_E019_00_DEM.tif` oraz
  `Copernicus_DSM_COG_10_N49_00_E020_00_DEM.tif`.
- GeographicLib `egm2008-2_5.tar.bz2`, zachowany wraz z rozpakowanym PGM/WLD/XML.
- OpenStreetMap przez Overpass API. Zachowano surową odpowiedź JSON i dokładne zapytanie:

```text
[out:json][timeout:120];
node["natural"="peak"](49.10,19.55,49.35,20.40);
out body;
```

Pełne URL-e, czas pobrania, ETagi i skróty znajdują się lokalnie w
`data/raw/download-metadata.json` oraz `data/raw/SHA256SUMS`. Atrybucja i odnośniki
do licencji są w `data/raw/ATTRIBUTION.txt`.

| Plik źródłowy | URL | SHA-256 |
|---|---|---|
| N49E019 GeoTIFF | [AWS GLO-30 N49E019](https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com/Copernicus_DSM_COG_10_N49_00_E019_00_DEM/Copernicus_DSM_COG_10_N49_00_E019_00_DEM.tif) | `5447343ad28e0cbd87ebe60c4edcd560f0c1dec3b8496c090dbcf5bcc716c690` |
| N49E020 GeoTIFF | [AWS GLO-30 N49E020](https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com/Copernicus_DSM_COG_10_N49_00_E020_00_DEM/Copernicus_DSM_COG_10_N49_00_E020_00_DEM.tif) | `97b3cab2fba6af094b0ee8a15a2d978ae8c7ad1a89004049cdb282f9897656de` |
| `egm2008-2_5.tar.bz2` | [GeographicLib geoids-distrib](https://downloads.sourceforge.net/project/geographiclib/geoids-distrib/egm2008-2_5.tar.bz2) | `d602e13446a4a4a23f39aecfe6a2a0760a1bc6c1b497482c2ebc9f7d513be699` |
| `tatras-peaks.json` | [Overpass API](https://overpass-api.de/api/interpreter) | `a08b4a1ee43bd65aa55d9e6291a3cee52991ab7cc094b18fc5996d863d10f08f` |

## Kontrola odbiorowa

GDAL 3.8.4 odczytał oba DEM-y jako jednopasmowe GeoTIFF/COG, EPSG:4326, Float32,
3600×3600, z pikselem 1/3600 stopnia i metadanymi `AREA_OR_POINT=Point`. Pliki nie
deklarują wartości NoData, a maska GDAL ma flagę `GMF_ALL_VALID`; brak NoData jest więc
jawną cechą tych dwóch kafli, a nie wartością zero.

Wartości DEM są wysokościami ortometrycznymi w metrach względem EGM2008 (EPSG:3855),
zgodnie ze specyfikacją produktu GLO-30 DGED. T11 musi je przeliczyć na wysokości
elipsoidalne WGS84; samo przepisanie informacji CRS byłoby błędem.

| Kafel | Zakres W,S,E,N według GDAL | Zakres wartości |
|---|---|---|
| N49E019 | 18.9998611111, 49.0001388889, 19.9998611111, 50.0001388889 | 203–2439.7817 m |
| N49E020 | 19.9998611111, 49.0001388889, 20.9998611111, 50.0001388889 | 183.8093–2603.9302 m |

Przesunięcie granic o pół piksela wynika z punktowej rejestracji COG. Oba kafle razem
pokrywają nominalny roboczy obszar 19.4–20.6°E, 49.0–49.6°N. Są to numeryczne rastry
DSM, nie zrzuty ekranu ani hillshade.

Geoidę odczytano jako siatkę 8640×4321, EPSG:4326, krok 2.5 minuty. Metadane opisują
`WGS84 EGM2008, 2.5-minute grid`, `AREA_OR_POINT=Point`, offset -108 m i skalę 0.003 m.

Eksport Overpass zawiera 1141 elementów; wszystkie są węzłami z
`natural=peak`, identyfikatorem, lat/lon i pełnym zestawem tagów. 1092 węzły mają
`name`, a 829 tag `ele`. Zakres rzeczywistych punktów to 19.5508293–20.3991664°E i
49.1015653–49.3496274°N. Ręczna kontrola nazw potwierdziła m.in. Rysy,
Gerlachovský štít/Gerlach, Kriváň i Svinica/Świnica, wraz z wariantami `name:pl`
i `name:sk`. Bbox obejmuje polską i słowacką stronę Tatr.

Integralność wszystkich wejść została sprawdzona poleceniem:

```sh
cd data/raw
sha256sum --check SHA256SUMS
```
