# T14: pierwsza rzeczywista scena Tatr

T14 łączy walidowany pakiet `tatras-v1` z dotychczasowym pipeline sceny, silnika,
wyniku JSON i SVG. Pozycja obserwatora jest **symulowana**, a nie zmierzona:
`49.3000°N, 19.9800°E`, 2 m nad powierzchnią DEM. Pole `observer.simulated=true`
jest zachowywane w `result.json` jako `metadata.observer_simulated=true`.

Pierwszy widok można odtworzyć jednym poleceniem:

```sh
./build/desktop-debug/tatra render \
  --dataset data/tatras-v1 \
  --scene examples/tatras-v1/south-east.json \
  --out out/tatras-v1/south-east
```

CLI najpierw ładuje i waliduje cały `Dataset`, a dopiero potem tworzy silnik z
referencjami do należących do niego `GridTerrain` i `PeakCatalog`. Wyniki zawierają
`dataset_id=tatras-v1` oraz SHA-256 manifestu
`2206037d24a47a1e5d825232a61d487d157f497e1ef339daefbd924a7ad75775`.

## Widoki i kontrola sektorów

Sceny w [`examples/tatras-v1`](../examples/tatras-v1/) mają tę samą kamerę
1280 × 720 px, HFOV 60°, pitch 5° i roll 0°. Heading wynosi kolejno 90°, 135°,
180° i 225°.

| Scena | W kadrze | VisibleInModel | OccludedInModel |
|---|---:|---:|---:|
| east | 0 | 0 | 0 |
| south-east | 31 | 6 | 25 |
| south | 14 | 2 | 12 |
| south-west | 14 | 1 | 13 |

Pusty widok wschodni potwierdza granicę sektora: najbliższy azymut katalogu leży
poza zakresem 60–120°. Kieżmarski Szczyt pojawia się w widoku południowo-wschodnim,
a Wielki Giewont w południowo-zachodnim. Dla szczytów wspólnych kolejnym kadrom
położenie zmienia się w oczekiwanym kierunku: Kościelec przechodzi z `u=1228.04`
do `u=307.03`, a Kopa Kondracka z `u=1180.80` do `u=266.54` przy obrocie kamery
o kolejne 45° zgodnie z ruchem wskazówek zegara.

To kontrola geometrii na modelu, nie walidacja wobec fotografii. Status
`VisibleInModel` uwzględnia wyłącznie DEM i nie opisuje aktualnych drzew, budynków
ani warunków atmosferycznych.

## Diagnostyka linii widzenia i styk kafli

Powtarzane `--debug-peak ID` włącza debugowy SVG i zapisuje wyłącznie wskazane
linie widzenia do `visibility-debug.json`:

```sh
./build/desktop-debug/tatra render \
  --dataset data/tatras-v1 \
  --scene examples/tatras-v1/south-east.json \
  --out out/tatras-v1/south-east-debug \
  --debug-peak osm:node:26862937 \
  --debug-peak osm:node:1862536196
```

Ślady do Gerlacha i Rysów przechodzą przez południk 20°, czyli styk dwóch
źródłowych kafli Copernicus. Zapisano odpowiednio 486 i 523 próbki; 5 i 6 próbek
leży w przedziale 19.9998–20.0002°. Wszystkie mają `terrain_state=Valid`.

Scena `outside-coverage.json` umieszcza symulowanego obserwatora tuż za zachodnią
krawędzią DEM. W wyniku 12 linii ma `MissingTerrain`, 38 kończy się wcześniej
potwierdzoną przeszkodą i żadna nie ma `VisibleInModel`.

## Test odbiorowy z pełnym pakietem

Pełne dane są ignorowane przez Git, dlatego test nie jest domyślnie częścią małych
fixture. Można go włączyć jawnie:

```sh
cmake --preset desktop-debug -DTATRA_REAL_DATASET_DIR="$PWD/data/tatras-v1"
cmake --build --preset desktop-debug --parallel
ctest --test-dir build/desktop-debug -R cli.render.tatras-v1 --output-on-failure
```

Test generuje cztery widoki, sprawdza metadane i ruch wspólnych nazw, eksportuje
ślady przez styk kafli oraz potwierdza zachowanie poza pokryciem.
