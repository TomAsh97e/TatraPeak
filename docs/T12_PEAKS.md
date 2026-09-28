# T12: katalog szczytów

`tatra-prepare peaks` importuje zapisany eksport Overpass, ogranicza go do jawnej,
wersjonowanej listy identyfikatorów, stosuje osobny plik korekt i pobiera wysokość
geometryczną z rastra utworzonego w T11:

```sh
./build/desktop-debug/tatra-prepare peaks \
  --raw-osm data/raw/osm/tatras-peaks.json \
  --terrain data/tatras-v1/terrain-ellipsoid.tif \
  --selection config/tatras-v1-peaks.json \
  --corrections config/tatras-v1-peak-corrections.json \
  --out data/tatras-v1
```

Wynik `data/tatras-v1/peaks.json` zawiera 50 rekordów. Import rzeczywistych danych
zakończył się raportem: 1141 elementów wejściowych, 1141 sparsowanych węzłów,
50 wybranych i przyjętych, 1091 pominiętych przez selekcję, 0 odrzuconych,
0 duplikatów oraz 0 brakujących lub błędnych wartości `ele` w wybranym podzbiorze.

## Reguły importu

- Stabilne id ma postać `osm:node:<id>`; rekordy o różnych id nie są łączone po
  nazwie ani współrzędnych.
- Nazwa prezentacyjna jest wybierana kolejno z `name:pl`, `name`, `name:sk`.
  Wszystkie trzy wartości źródłowe pozostają w `peaks.json`.
- Brak `ele` lub wartość niebędąca jednoznaczną liczbą nie oznacza zera. Szczyt
  pozostaje w katalogu bez `display_elevation`, a raport zapisuje przyczynę.
- `display_elevation` z OSM ma pionowy układ `unspecified` i służy tylko do
  etykiety. `geometry_position.ellipsoid_height_m` pochodzi z
  `terrain-ellipsoid.tif` (domyślnie) i jest używana do obliczeń. Jawny wyjątek
  opisano poniżej.
- Brak pokrycia lub NoData w DEM odrzuca rekord z przyczyną `outside_dem` albo
  `dem_nodata`.
- Powtórzony ten sam identyfikator OSM może zostać scalony tylko przy zgodnych
  współrzędnych i danych. Konflikt duplikatów kończy import błędem.

Plik [tatras-v1-peak-corrections.json](../config/tatras-v1-peak-corrections.json)
jest obecnie pusty: kontrola wybranego podzbioru nie wykazała poprawki, którą można
udokumentować lepszym źródłem. Format wymaga dla każdej przyszłej poprawki pola
`note`; oryginalne nazwy i `ele` nadal zostają zachowane w wyniku.

## Jawna korekta wysokości geometrycznej

Ostry wierzchołek może mieć inną wysokość niż próbka DEM pod jego współrzędnymi.
Importer obsługuje opcjonalne `geometry_height_correction`, niezależne od
`display_elevation`. Wartość musi być już wysokością **elipsoidalną WGS84**:

```json
{
  "schema_version": 1,
  "corrections": [{
    "id": "osm:node:605282386",
    "geometry_height_correction": {
      "ellipsoid_height_m": 2110.8089,
      "source": "Próba IMG_9553: OSM ele 2068 m; założono zgodność z EGM2008; N=42.8089 m; h=H+N"
    },
    "note": "Przykład eksperymentalny: datum OSM nieokreślone, wysokość niezweryfikowana pomiarem."
  }]
}
```

`source` i `note` są wymagane, a wysokość musi być skończoną liczbą.
Katalog zachowuje korektę i jej źródło, oznaczając szczyt jako `ExplicitEllipsoid`;
to oznaczenie nie poświadcza dokładności. Manifest podaje `Mixed`, jeżeli katalog
zawiera również szczyty `DemSample`, albo `ExplicitEllipsoid`, gdy skorygowano wszystkie.
Po zmianie katalogu trzeba ponownie uruchomić `tatra-prepare manifest`.

Raster i współrzędne szczytu pozostają niezmienione. Pokrycie poprawnym DEM jest
nadal wymagane. Loader sprawdza zgodność wartości korekty z pozycją szczytu,
obecność jej źródła i notatki oraz deklarację manifestu; dla `DemSample` nadal
wymaga zgodności wysokości z rastrem. Nie ma automatycznego zastępowania DEM
wartością OSM `ele` o nieznanym układzie wysokościowym.

## Ręczna kontrola wyboru

Sprawdzono identyfikator, nazwę preferowaną, współrzędne, obecność `ele` i pokrycie
DEM każdego wpisu. Lista obejmuje Tatry Wysokie i Zachodnie po obu stronach granicy.
Dwa różne węzły Rysów pozostają osobnymi rekordami, co celowo potwierdza brak
deduplikacji po nazwie.

| # | OSM node | Nazwa | `ele` OSM [m] | DEM ellipsoid [m] |
|---:|---:|---|---:|---:|
| 1 | 26862937 | Gerlach | 2655 | 2641.9 |
| 2 | 891526548 | Łomnica | 2633 | 2487.3 |
| 3 | 380717332 | Lodowy Szczyt | 2628 | 2574.6 |
| 4 | 385101740 | Durny Szczyt | 2621 | 2564.4 |
| 5 | 385080085 | Zadni Gerlach | 2616 | 2611.5 |
| 6 | 380695970 | Kieżmarski Szczyt | 2556 | 2523.1 |
| 7 | 385080084 | Wysoka | 2547 | 2491.1 |
| 8 | 385118677 | Kończysta | 2536 | 2536.5 |
| 9 | 385126469 | Baranie Rogi | 2526 | 2516.1 |
| 10 | 1862536196 | Rysy | 2503 | 2497.5 |
| 11 | 4366169405 | Rysy | 2499 | 2509.2 |
| 12 | 260278694 | Krywań | 2494 | 2453.2 |
| 13 | 385125633 | Staroleśny Szczyt | 2489 | 2455.8 |
| 14 | 385095983 | Ganek | 2462 | 2338.5 |
| 15 | 380721955 | Sławkowski Szczyt | 2452 | 2485.8 |
| 16 | 385126354 | Batyżowiecki Szczyt | 2448 | 2293.6 |
| 17 | 385113132 | Pośrednia Grań | 2441 | 2403.9 |
| 18 | 615589377 | Mięguszowiecki Szczyt Wielki | 2438 | 2322.2 |
| 19 | 385115017 | Niżnie Rysy | 2430 | 2348.9 |
| 20 | 380705790 | Czarny Szczyt | 2429 | 2397.5 |
| 21 | 382487001 | Mała Wysoka | 2429 | 2385.3 |
| 22 | 605282380 | Rumanowy Szczyt | 2428 | 2324.6 |
| 23 | 943064021 | Hruby Wierch | 2428 | 2398.6 |
| 24 | 5131055060 | Szatan | 2421 | 2412.7 |
| 25 | 385120972 | Kołowy Szczyt | 2418 | 2356.2 |
| 26 | 604540323 | Jaworowy Szczyt | 2418 | 2413.5 |
| 27 | 380745908 | Koprowy Wierch | 2367 | 2336.9 |
| 28 | 321624997 | Cubryna | 2376 | 2338.4 |
| 29 | 943081573 | Furkot | 2403 | 2397.3 |
| 30 | 3343362920 | Świnica | 2302 | 2278.6 |
| 31 | 460928401 | Kozi Wierch | 2291 | 2287.6 |
| 32 | 355985949 | Kościelec | 2155 | 2065.5 |
| 33 | 3494334728 | Zawratowa Turnia | 2247 | 2199.4 |
| 34 | 3503795970 | Orla Baszta | 2177 | 2138.7 |
| 35 | 2103273001 | Wielka Koszysta | 2193 | 2220.4 |
| 36 | 3552158247 | Kasprowy Wierch | 1987 | 2022.6 |
| 37 | 1295278143 | Wielki Giewont | 1894.5 | 1885.3 |
| 38 | 3560695684 | Kopa Kondracka | 2005 | 2041.3 |
| 39 | 496805360 | Małołączniak | 2096 | 2137.3 |
| 40 | 496805357 | Ciemniak | 2096 | 2134.4 |
| 41 | 495957137 | Bystra | 2248 | 2274.6 |
| 42 | 1294543835 | Starorobociański Wierch | 2176 | 2209.7 |
| 43 | 495956991 | Jakubina | 2194 | 2217.9 |
| 44 | 495953908 | Baraniec | 2182 | 2217.4 |
| 45 | 3560920780 | Jarząbczy Wierch | 2137 | 2171.5 |
| 46 | 2910369166 | Banówka | 2178 | 2210.0 |
| 47 | 1674619681 | Rohacz Ostry | 2088 | 2074.6 |
| 48 | 2302916813 | Wołowiec | 2064 | 2094.1 |
| 49 | 946080669 | Rakoń | 1879 | 1918.3 |
| 50 | 1735156627 | Rohacz Płaczliwy | 2125 | 2146.9 |

Różnice między `ele` a próbką DEM są oczekiwane: `ele` ma nieokreślony pionowy
układ odniesienia, a wysokość geometryczna pochodzi z interpolowanego DSM GLO-30
i została przeliczona na elipsoidę. Wartości nie są wzajemnie zastępowane.

SHA-256 wyniku tego przebiegu:
`4db51b5bcb0dcb0e1582ceabbe1ea55ca89c223354da30bf416cfed014d90b14`.
