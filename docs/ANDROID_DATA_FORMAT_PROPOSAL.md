# Pakiet Android TATRAPK v1

Status: **zatwierdzony 2026-10-06 i wdrożony**. Nie zmienia `tatras-v1`, publicznego
C ABI ani formatu sesji T16.

Jeden plik `tatras-v1.tpk`, bez kompresji, little-endian. Brak GeoTIFF/JSON/GDAL
w runtime natywnym aplikacji. Desktop czyta istniejący, zwalidowany pakiet
przez obecny adapter; Android sprawdza plik przed utworzeniem silnika.

## Nagłówek — 168 bajtów

| Offset | Typ | Znaczenie |
|---:|---|---|
| 0 | 8 bajtów | magic `TATRAPK\0` |
| 8 | UInt32 | wersja = 1 |
| 12 | UInt32 | długość nagłówka = 168 |
| 16 | UInt64 | długość całego pliku |
| 24, 28 | UInt32 × 2 | rows, columns |
| 32, 40 | Float64 × 2 | latitude/longitude środka pierwszej komórki, stopnie WGS84 |
| 48, 56 | Float64 × 2 | kroki latitude/longitude, stopnie, również ujemne |
| 64 | UInt32 | liczba szczytów |
| 68 | UInt32 | zarezerwowane = 0 |
| 72 | UInt32 | długość identyfikatora datasetu UTF-8 |
| 76 | UInt32 | zarezerwowane = 0 |
| 80 | UInt64 | długość sekcji wysokości = rows × columns × 4 |
| 88 | UInt64 | długość maski = rows × columns |
| 96 | UInt64 | długość sekcji szczytów |
| 104 | 32 bajty | SHA-256 całego pliku z wyzerowanym polem 104–135 |
| 136 | 32 bajty | SHA-256 manifestu wejściowego zwalidowanego pakietu tatras-v1 |

Hash wykrywa uszkodzenie i identyfikuje dokładne dane; nie jest podpisem cyfrowym.
Konwerter najpierw uruchamia istniejącą walidację plików/hashów pakietu źródłowego.

## Sekcje w kolejności

1. Identyfikator datasetu: dokładnie podana liczba bajtów UTF-8, bez NUL.
2. Wysokości Float32, row-major, **metry elipsoidalne WGS84**.
3. Maska UInt8, row-major, tylko 0 (brak danych) albo 255 (ważna wysokość).
4. Katalog szczytów: rekordy o stałym prefiksie 48 bajtów i zmiennej części tekstowej.

Rekord szczytu: długości id i nazwy (UInt32 × 2), latitude/longitude/wysokość
geometryczna elipsoidalna (Float64 × 3), flaga wysokości opisowej (UInt8 0/1),
7 zerowych bajtów rezerwy, wysokość opisowa (Float64; zero, jeśli flaga = 0),
następnie id UTF-8 i nazwa UTF-8. Wysokość opisowa jest kopiowana z katalogu,
nie wyprowadzana z wysokości geometrycznej. Nie dodajemy domyślnego „n.p.m.”
bez informacji o układzie wysokości w danych źródłowych.

## Walidacja i pamięć

Loader najpierw strumieniowo weryfikuje rozmiar i SHA-256; dopiero potem dekoduje.
Wszystkie mnożenia/sumy liczności kontrolują overflow. Odrzuca nieznaną wersję,
niezerowe rezerwy, końcowe dodatkowe bajty, ucięte sekcje, niepoprawne UTF-8,
NUL, duplikaty id, niepoprawne współrzędne i nie-skończone ważne wysokości.

Jawne limity v1: plik ≤ 256 MiB, ≤ 40 mln komórek, ≤ 100 tys. szczytów,
≤ 4096 bajtów na napis; rows/columns ≥ 2. Limity chronią Galaxy S10e przed
niekontrolowaną alokacją, ale szczytowe zużycie pamięci trzeba zmierzyć na telefonie:
C ABI kopiuje raster, a adapter JNI obecnie także przygotowuje bufory pośrednie.
Po create loader zwalnia wejściowe tablice. Konwerter odrzuca pakiet przekraczający limity.

Zasób trafia do `android/app/src/main/assets/datasets/` lokalnie (duży plik
ignorowany w Git); aplikacja rozróżnia brak pakietu od uszkodzenia i nadal
udostępnia diagnostykę syntetyczną offline.

```sh
cmake --preset desktop-debug
cmake --build --preset desktop-debug --target tatra-prepare
mkdir -p android/app/src/main/assets/datasets
./build/desktop-debug/tatra-prepare mobile \
  --dataset data/tatras-v1 \
  --out android/app/src/main/assets/datasets/tatras-v1.tpk
```
