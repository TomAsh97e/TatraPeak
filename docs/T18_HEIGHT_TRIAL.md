# IMG_9553: próba korekty wysokości szczytów

Lokalne artefakty: `out/t18/IMG_9553/`. Zdjęcie i dane pozostają ignorowane przez Git.
Próba nie zamyka walidacji T18: obejmuje jedno zdjęcie, dwa punkty kalibracyjne,
dwa punkty kontrolne i jeden wierzchołek częściowo zasłonięty chmurą.

Stanowisko podane przez użytkownika: 49.2011388889 N, 20.0712777778 E.
Przyjęto grunt 1410 m n.p.m. + telefon 1,65 m oraz geoidę EGM2008 (N=42,7577 m),
co daje wysokość kamery 1454,4077 m elipsoidalnie. Układ wysokości z mapy
nie jest potwierdzony. Oryginał: 5712×4284, iPhone 15 Pro, 1×, bez kadrowania.
Parametry wewnętrzne oszacowano z ogniskowej równoważnej 24 mm i pozostawiono stałe.

Do pięciu wysokości OSM `ele` dodano lokalne N z `GeoidEval egm2008-2_5`.
Założono zgodność nieokreślonego datum OSM z EGM2008. Te wysokości służą
eksperymentowi; nie są niezależną, zweryfikowaną referencją geodezyjną.
Korekty mają źródło wraz z SHA-256 wejściowego eksportu OSM i zapis założenia.
Nowy pakiet `dataset-height-corrected` zachowuje oryginalny raster i współrzędne.

| Szczyt | DEM elipsoidalnie [m] | OSM + N [m] | Zmiana [m] |
|---|---:|---:|---:|
| Wielki | 2322,21 | 2480,82 | +158,61 |
| Cubryna | 2338,43 | 2418,82 | +80,40 |
| Czarny | 2390,03 | 2452,81 | +62,78 |
| Pośredni | 2324,71 | 2431,82 | +107,10 |
| Mnich | 1986,24 | 2110,81 | +124,57 |

Heading/pitch/roll dopasowano tylko na Cubrynie i Mnichu, z zachowaniem wcześniej
wybranych punktów oznaczonych na zdjęciu i potwierdzonych przez użytkownika.
Porównanie dotyczy poprzedniej próby z poprawioną wysokością obserwatora i swobodnym roll:

| Szczyt | Rola | Przed [px] | Po [px] |
|---|---|---:|---:|
| Cubryna | kalibracja | 4,71 | 38,09 |
| Mnich | kalibracja | 4,19 | 34,15 |
| Wielki | kontrola | 254,60 | 65,57 |
| Pośredni | kontrola, punkt przybliżony | 193,36 | 92,68 |
| Czarny | chmury, poza oceną kontrolną | 284,64 | 175,80 |

Błędy odnoszą się do oryginalnej rozdzielczości. Wynik orientacji:
heading 203,737°, pitch 6,470°, roll 1,238° (wcześniej roll 10,677°).
Wszystkie pięć punktów ma status `VisibleInModel`. To ocena względem obecnego
DEM, nie dowód rzeczywistej widoczności. Raster nadal może niedoszacowywać grzbiety.

Poprawa punktów kontrolnych wspiera hipotezę o istotnym wpływie wysokości szczytów.
Nie dowodzi, że DEM jest jedyną przyczyną błędu: pozostają niepewność pozycji,
oznaczeń, ogniskowej i wysokości. Mały poprzedni błąd dwóch punktów dopasowania
nie oznaczał poprawnej orientacji całego zdjęcia.

`overlay.svg` zawiera oryginalne zdjęcie, żółte potwierdzone oznaczenia i cyjanowe
projekcje silnika. `photo-trial-report.json` zapisuje założenia, korekty, parametry,
błędy przy poprzedniej orientacji oraz po ponownym dopasowaniu.
Poprzednie zdjęcie z nakładką i raport zachowano w `before-peak-heights/`.

## Druga próba: widok z Rysów

Drugą próbę wykonano na pliku 1824×1368 bez EXIF. Użytkownik podał stanowisko
na Rysach oraz Samsung Galaxy S23 z aparatem 1×. Przyjęto oficjalne pole widzenia
głównego aparatu 85°. Ponieważ nie było wiadomo, z którego wierzchołka wykonano
zdjęcie, policzono osobno pozycję słowackiego wierzchołka 2503 m i polskiego 2499 m.

Gerlach i Wysoką wykorzystano jako wstępne punkty kalibracyjne. Kończystą pozostawiono
jako przybliżony punkt kontrolny. Dla słowackiego wierzchołka otrzymano błędy 31 px
dla Gerlacha, 28 px dla Wysokiej i 63 px dla Kończystej. Dla polskiego wierzchołka
było to odpowiednio 41 px, 38 px i 65 px. Różnica punktu kontrolnego wyniosła mniej
niż 3 px, więc zdjęcie nie pozwala rozstrzygnąć, na którym wierzchołku stał autor.

Druga próba nie jest niezależną walidacją: Gerlach jest częściowo zasłonięty chmurą,
identyfikacja Kończystej wymaga potwierdzenia, a pole widzenia pochodzi ze specyfikacji
telefonu zamiast z EXIF. Pokazuje jednak, że ręczna kalibracja jest wykonalna również
dla pliku bez metadanych. Jej punkt kontrolny ma błąd około 3,4% szerokości obrazu.

Zdjęcia, skrypty robocze, pełne raporty JSON i nakładki SVG pozostają lokalnie w
ignorowanym przez Git katalogu `out/t18/`. Repozytorium przechowuje wnioski oraz
przetestowany mechanizm korekt, bez publikowania prywatnych zdjęć.
