Ten katalog służy do lokalnego przechowywania surowych i przygotowanych danych.
Zawartość poza tym plikiem jest ignorowana przez Git.

Pozyskiwanie danych opisuje docs/MVP_PLAN.md; przygotowanie obejmuje T10–T13.
Docelowy pakiet: manifest.json, terrain-ellipsoid.tif, peaks.json, ATTRIBUTION.txt.
Źródła i licencje danych przechowuj niezależnie od kodu. Nie dodawaj tu tokenów.

Po wykonaniu T10 lokalne, ignorowane przez Git wejścia znajdują się w `data/raw/`.
Zawierają dwa kafle Copernicus DEM GLO-30, model `egm2008-2_5`, eksport Overpass,
metadane pobrania, atrybucję i sumy SHA-256. Wyniki kontroli odbiorowej oraz dokładne
źródła są zapisane w [docs/T10_DATA.md](../docs/T10_DATA.md).

Po wykonaniu T11 `data/tatras-v1/terrain-ellipsoid.tif` zawiera lokalny, ignorowany
przez Git raster po mozaikowaniu, przycięciu i konwersji EGM2008 → WGS84 ellipsoid.
Kontrakt metadanych i kontrolę z `GeoidEval` opisuje
[docs/T11_DEM.md](../docs/T11_DEM.md).

T12 tworzy obok rastra `data/tatras-v1/peaks.json` z 50 ręcznie sprawdzonymi
szczytami. Wersjonowana selekcja i osobny plik korekt znajdują się w `config/`,
a reguły importu, raport oraz kontrola rekordów w
[docs/T12_PEAKS.md](../docs/T12_PEAKS.md).

T13 zamyka katalog `data/tatras-v1/` jako walidowany pakiet: dodaje deterministyczny
`manifest.json` i `ATTRIBUTION.txt`. Tworzenie, kontrakt spójności oraz polecenie
`tatra dataset-info` opisuje [docs/T13_DATASET.md](../docs/T13_DATASET.md).

T14 używa tego pakietu do pierwszych symulowanych panoram rzeczywistego terenu.
Sceny, polecenie `tatra render`, kontrolę sektorów i diagnostykę linii widzenia
opisuje [docs/T14_DEMO.md](../docs/T14_DEMO.md).
