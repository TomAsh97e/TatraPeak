# T16: deterministyczny replay sesji

Replay odtwarza czasy zapisane w danych. Nie czyta zegara ściennego, nie czeka i nie
używa `sleep`, dlatego szybkość wykonania programu nie wpływa na wynik merytoryczny.
Czas wykonania nie jest częścią `report.json` i może być mierzony osobno.

## Format sesji

Katalog sesji zawiera `session.json` oraz wskazane przez niego pliki próbek i klatek:

```json
{
  "schema_version": 1,
  "observer": {
    "latitude_deg": 49.3,
    "longitude_deg": 19.98,
    "height": {"mode": "above_dem", "metres": 2.0},
    "simulated": true
  },
  "camera": {
    "width_px": 1280,
    "height_px": 720,
    "horizontal_fov_deg": 60.0
  },
  "options": {"min_range_m": 500.0, "max_range_m": 30000.0},
  "max_gap_ns": 1000000000,
  "poses_file": "poses.json",
  "frames_file": "frames.json"
}
```

Każda orientacja ma nieujemny, ściśle rosnący `timestamp_ns`. Kwaternion zawsze ma
nazwane pola i jawny kierunek transformacji:

```json
{
  "timestamp_ns": 0,
  "camera_from_enu": {"w": 0.7071, "x": 0.7071, "y": 0.0, "z": 0.0}
}
```

`frames.json` jest niepustą tablicą obiektów z `timestamp_ns` i opcjonalnym
`image_path`. Ścieżki są względne wobec katalogu sesji i nie mogą zawierać `..`.
Obraz nie musi istnieć: na etapie T16 jego ścieżka jest jedynie opcjonalnym tłem
SVG. `max_gap_ns` ogranicza cały przedział próbek użyty do interpolacji.

## Uruchomienie

Przykładową sesję można odtworzyć na pakiecie Tatr:

```sh
./build/desktop-debug/tatra replay \
  --dataset data/tatras-v1 \
  --session examples/session \
  --out out/replay
```

Do szybkiej próby bez pakietu danych służy deterministyczny generator i teren
syntetyczny:

```sh
./build/desktop-debug/tatra generate-session --out out/session-control
./build/desktop-debug/tatra replay \
  --dataset synthetic --session out/session-control --out out/replay-control
```

Warianty z kontrolowanym błędem heading i opóźnieniem danych orientacji:

```sh
./build/desktop-debug/tatra generate-session \
  --out out/session-heading-error --heading-error-deg 10
./build/desktop-debug/tatra generate-session \
  --out out/session-delay --pose-delay-ns 250000000
```

`--duration-ns` skaluje wspólną oś czasu próbek i klatek. Domyślna sesja obraca
kamerę od północy do wschodu w ciągu sekundy i zawiera klatki na początku, w
połowie oraz na końcu.

## Wynik i błędy klatek

`report.json` zawiera wersję programu, identyfikator i hash pakietu, parametry
sesji, liczbę wykonanych i pominiętych klatek oraz rekord każdej klatki. Poprawne
klatki trafiają do katalogów `frames/000000-<timestamp_ns>/` jako `result.json` i
`overlay.svg`.

Brak orientacji albo zbyt duża luka nie używa ostatniej dostępnej próbki. Klatka
otrzymuje status `skipped` oraz kod `TimeOutsideSession` lub `TimeGapTooLarge`, a
replay kontynuuje następne klatki. Poprawnie przetworzona sesja kończy się kodem
wyjścia 0 również wtedy, gdy raport zawiera pominięte klatki. Błąd formatu, danych
wejściowych lub zapisu jest błędem całego polecenia.

## Testy

```sh
ctest --test-dir build/desktop-debug -R 'SessionReplay|cli.replay' --output-on-failure
```

Testy porównują dwa wykonania tej samej sesji, kontrolują kierunek przesunięcia
projekcji po dodaniu błędu heading i opóźnienia, wymagają jawnego błędu brakującej
orientacji oraz porównują trajektorie o czasie trwania 1 s i 2 s.
