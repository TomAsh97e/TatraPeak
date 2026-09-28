# T19: koszt analizy IMG_9553

## Wynik i decyzja

Zachowano jedną małą optymalizację: cztery stałe granice/indeksy `GridTerrain`
są obliczane przy konstrukcji zamiast przy każdej próbce. `sampleHeight` skrócił
czas o **8,18%** za **32 bajty na cały raster**. Czas całego `evaluate` jest
praktycznie niezmieniony: **0,2444 → 0,2446 ms**. Nie wykazano przyspieszenia całego
silnika i nie ma tu uzasadnienia dla bardziej złożonego cache, SIMD ani alokatora.

Pomiar: 2026-09-28, siedem naprzemiennych par referencja/kandydat na jednym CPU.
[Raport maszynowy](../benchmarks/results/img_9553.json) zawiera wszystkie próbki
czasów, metadane i porównania. [Instrukcja](../benchmarks/README.md) opisuje build,
odtworzenie pakietu, uruchomienie i porównanie wyników.

## Środowisko i wejście

- Intel Core i7-12700KF, 20 logicznych CPU, affinity `[2]`; RAM 32 678 400 KiB
  (31,16 GiB według `/proc/meminfo`). Linux Mint 22.1, Linux 6.8.0-142-generic x86_64.
- GCC 13.3.0, C++20, Release `-O3 -DNDEBUG`; ostrzeżenia
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`, bez LTO i `-ffast-math`.
  Bez równoległego builda/testów podczas pomiarów; taktowanie turbo nie było blokowane.
- Referencja: rdzeń z `cedc27cf0b863dc5606dbac5f96df234951902fa` + runner T19.
  Kandydat: ten sam rdzeń z cache czterech `double` w `terrain.hpp`/`terrain.cpp`.
  Identyczny kod runnera w porównywanych plikach wykonywalnych. Ten sam HEAD w
  metadanych oznacza bazę niezatwierdzonej w chwili pomiaru poprawki, nie brak różnicy.
- Pakiet `morskie-oko-height-trial`, 53 szczyty, raster 2160×4321;
  Copernicus GLO-30 (2021), EGM2008 2.5', OSM z 2026-09-25 i pięć jawnych korekt T18.
  SHA-256 manifestu: `235eb73eb33483d1ac2a6c8163a327c44cdcd26d0a5293d5b1b47f679607f4c4`.
  SHA-256 DEM: `943edd67c853bc1b8c5074e7c9b18c82620182f3111944419da11ec61c3c2a0c`.
  SHA-256 katalogu: `0552e92438b7c5650cb065df8077044477bda90f5d3a215bb38d606dafd478fe`.
- Scena IMG_9553: 5712×4284, fx=fy=3960,5594 px, cx=2856, cy=2142;
  heading/pitch/roll 203,7370232° / 6,4704° / 1,237824°.
  Kamera 49,2011388889 N, 20,0712777778 E, h=1454,4077 m elipsoidalnie.
- Zasięg 50–10 000 m, near=0,01 m, krok widoczności 10 m, pas niepewności 0,5 m,
  guard obserwatora 2 m i celu 30 m, limit 10 000 próbek.
  Kadr: 17 szczytów poza zasięgiem, 36 projektowanych, 1588 próbek widoczności.
  Pełne wejście zapisano w fixture i JSON-ie raportu; nie użyto dekodowania zdjęcia.

## Metoda i pomiary

W każdym przebiegu: 3 partie rozgrzewki, potem 31 partii pomiarowych.
Parametr bazowy `--iterations 20`. Zestawy i liczby operacji na partię:

| Pomiar | Wejście | Operacje / partię |
|---|---|---:|
| project | wszystkie 53 punkty katalogu w ENU, ta sama kolejność | 10 000 × 53 |
| sampleHeight | siatka 64×64 wokół obserwatora, krok 0,0001° | 100 × 4096 |
| traceVisibility widoczny | kontrolny promień ~890 m nad płaskim DEM, 86 próbek | 100 |
| traceVisibility przeszkoda | grzbiet przy obserwatorze, blocker 26,94 m, 3 próbki | 10 000 |
| traceVisibility Mnich | rzeczywisty pakiet/scena, 163 próbki | 100 |
| evaluate | pełna analiza jednego kadru IMG_9553 | 20 |

Narzut pętli i wyliczenia sumy kontrolnej jest wliczony w czas; jej zapis do
`volatile` jest poza zegarem. Nie stosowano odejmowania kosztu pustej pętli.

W tabeli poniżej mediana oznacza medianę siedmiu median przebiegów. p95 liczono
metodą nearest-rank ze wszystkich 217 średnich partii na wersję, po normalizacji
do pojedynczej operacji; nie jest to p95 czasu indywidualnego wywołania.
Dodatnia redukcja oznacza krótszy czas.

| Pomiar | Mediana przed → po [ns] | p95 przed → po [ns] | Redukcja mediany |
|---|---:|---:|---:|
| project | 4,085 → 4,087 | 4,118 → 4,135 | −0,04% |
| sampleHeight | 12,266 → 11,263 | 12,330 → 11,371 | +8,18% |
| traceVisibility widoczny | 12 783,20 → 12 787,31 | 12 847,08 → 12 861,10 | −0,03% |
| traceVisibility przeszkoda | 556,805 → 555,761 | 569,746 → 560,235 | +0,19% |
| traceVisibility Mnich | 24 061,91 → 24 103,63 | 24 214,25 → 27 451,28 | −0,17% |
| evaluate | 244 371,55 → 244 631,05 | 245 435,20 → 245 375,50 | −0,11% |

Zysk `sampleHeight` wystąpił we wszystkich siedmiu parach. Wahania pozostałych
median nie uzasadniają deklarowania przyspieszenia; w jednym przebiegu Mnicha
wystąpił skok czasu, zachowany w raporcie (widoczny w p95). Nie odrzucano wolnych
próbek. Pierwsza eksploracyjna para sugerowała ~1,8% dla `evaluate`, ale kolejne
pary tego nie potwierdziły, więc nie jest podstawą wniosku.

Odczyt i walidacja danych są osobnym pomiarem: mediana siedmiu uruchomień
170,808 ms przed i 170,736 ms po. Nie opróżniano cache systemu plików.
Rozgrzewka ma osobny licznik dla każdej funkcji. Przygotowanie danych, kontrola
równoważności i zapis JSON są poza sekcjami analizy; SVG nie jest generowane.

## Pamięć i zgodność

`sizeof(GridTerrain)`: **104 → 136 B**, czyli +32 B; bez dodatkowej alokacji i bez
narzutu na piksel. Wysokości Float32 i maska zajmują nadal **46 666 800 B** (44,50 MiB).
Szczytowy RSS procesu do końca analizy: przed 137 980–138 240 KiB (mediana 138 080),
po 138 080–138 240 KiB (mediana 138 240). Różnica RSS nie mierzy kosztu cache:
obejmuje GDAL, strony pamięci, fixture i JSON-y; koszt samej zmiany wynika z `sizeof`.

Cache jest własnością niezmiennego rastra, kopiowany i przenoszony razem z nim.
Nie wymaga unieważniania ani synchronizacji. Walidacja north-up gwarantuje znaki
kroków. Zachowano kolejność działań interpolacji, dzielenia, reguły brzegów i NoData.

Każda para przeszła dokładne porównanie wartości JSON bez epsilonu: 53 projekcje,
4096 wysokości, trzy promienie, pełny wynik kadru i dziewięć orientacji ±0,4 rad
z deterministycznymi timestampami. SHA-256 znormalizowanej migawki semantycznej:
`1f65a374c3f2c66432c3708269897632e4abc8b385736152b927f38f2dc21538`.
Pełne migawki pozostają w lokalnym `out/t19/repeat-*/report.json`; raport w repo
przechowuje skrót i surowe pomiary, nie prywatną fotografię.

Nowe niezależne testy terenu: interpolacja znanej płaszczyzny, wszystkie krawędzie,
`nextafter` poza każdą granicą, NaN/Inf, kopiowanie/przenoszenie oraz zerowe wagi NoData.
Test benchmarku sprawdza powtarzalność i odrzucenie zmienionej referencji/błędnych
argumentów; cztery testy Pythona sprawdzają matematykę raportu i kontrolę porównań.

Buildy i testy końcowe: Desktop Release **78/78**, Desktop Debug **77/77** (w tym
prawdziwy pakiet Tatr), rdzeń Release bez adapterów **34/34**, ASan/UBSan **34/34**.
Sanitizery uruchomiono poza sandboxem, ponieważ LeakSanitizer nie działał pod
ograniczeniami ptrace; nie wyłączano wykrywania wycieków.

To pomiar wydajności i regresji obliczeń, nie nowa walidacja dokładności T18 ani
obietnica podobnego czasu na telefonie. Kalibracja, wysokości i nakładka zdjęcia
nie zostały zmienione.

## Cztery odpowiedzi po zadaniu

1. Wejście: zwalidowany pakiet i scena albo deterministyczny wariant syntetyczny,
   liczby powtórzeń i opcjonalna referencja. Wynik: raport czasów, pamięci,
   konfiguracji i wartości kontrolnych; porównanie może zakończyć się błędem.
2. Dataset posiada teren i katalog; żyje dłużej niż silnik używający const referencji.
   Cache granic należy do `GridTerrain`, a listy punktów i raporty do runnera.
3. Płaszczyzna testowa ma wysokość wyliczoną niezależnym prostym wzorem, fixture
   widoczności mają kontrolowany teren, a wynik produkcyjny porównano ze starym
   plikiem wykonywalnym. Statystyki sprawdzono na ręcznie policzonych liczbach.
4. Niepoprawny pakiet/scena/liczba powtórzeń, błąd zapisu albo niezgodność
   referencji daje komunikat i niezerowy kod. Brak pokrycia i NoData pozostają
   jawnymi stanami. Raport ujawnia brak danych pamięci i ograniczenia pomiaru.
