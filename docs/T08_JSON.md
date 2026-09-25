# Sceny i wyniki JSON (T08)

Build i przykład działający bez danych Tatr:

```sh
cmake --preset desktop-debug
cmake --build --preset desktop-debug
ctest --preset desktop-debug
build/desktop-debug/tatra render --dataset synthetic --scene examples/synthetic-scene.json --out out/synthetic
```

Polecenie zapisuje `out/synthetic/result.json`. `synthetic-v1` to wbudowany teren
testowy wokół (0°, 0°): płaska powierzchnia, grań i pas NoData oraz pięć szczytów.
Nie reprezentuje Tatr. Odczyt rzeczywistych pakietów jest zadaniem T13, SVG — T09.
Przy `TATRA_BUILD_DESKTOP_IO=OFF` rdzeń pozostaje dostępny, a `render` zwraca kod 2.

Scena wymaga `schema_version: 1`, całkowitego nieujemnego `timestamp_ns` mieszczącego
się w int64 oraz obiektów `observer`, `camera`, `orientation`.

- `observer`: `latitude_deg`, `longitude_deg`, `height: {mode, metres}`. Tryb
  `ellipsoid` oznacza wysokość elipsoidalną WGS84; `above_dem` dodaje `metres` do
  poprawnej próbki terenu. Brak próbki jest błędem wejścia.
- `camera`: dodatnie całkowite `width_px`, `height_px` oraz albo
  `horizontal_fov_deg`, albo komplet `fx_px`, `fy_px`, `cx_px`, `cy_px` bezpośrednio
  w tym samym obiekcie. Podanie obu wariantów jest odrzucane jako niejednoznaczne.
- `orientation`: `heading_deg`, `pitch_deg`, `roll_deg` zgodne z konwencją MVP.
- Opcjonalne `options`: `min_range_m`, `max_range_m`, `near_z_m` i obiekt
  `visibility` z `sample_step_m`, `clearance_band_m`, `start_guard_m`,
  `target_guard_m`, `max_samples`. Pominięte opcje mają domyślne wartości C++.
  Jawne `null`, złe typy i błędne wartości nie są zastępowane domyślnymi.

Nieznane pola, duplikaty kluczy, nieobsługiwane tryby i liczby niefinitywne są
odrzucane. Pliki referencyjne w `tests/fixtures/t08` są zapisane ręcznie.

Wynik ma `schema_version: 1`, czas, metadane uruchomienia, wszystkie opcje,
diagnostykę, listę `observations` i listę `warnings`. Obserwacje zawierają dane
szczytu, kierunek, projekcję i widoczność. Wysokość katalogowa jest oddzielona od
geometrycznej. Nieobliczona widoczność, brak piksela za kamerą i brak wartości
opcjonalnej są zapisywane jako `null`. `OutsideFrame` zachowuje piksel.
Stan `MissingTerrain` dodaje ostrzeżenie do wyniku; częściowy teren nie zmienia
kodu sukcesu. Pusty `dataset_sha256` dla danych wbudowanych oznacza brak pliku
pakietu do haszowania.

Kody wyjścia: **0** — sukces, **2** — błędne lub nieobsługiwane wejście,
**3** — błąd odczytu/zapisu. Writer serializuje dane przed otwarciem wyniku,
aby błąd danych nie wyzerował istniejącego pliku; błędy zapisu są raportowane,
ale zapis nie jest transakcją atomową.
