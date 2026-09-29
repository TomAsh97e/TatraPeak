# T20: wersja demonstracyjna i przenośność

T20 domyka wersję repozytorium możliwą do pokazania i przygotowuje rdzeń do
osadzenia w aplikacji Android/iOS. Nie jest to gotowa aplikacja mobilna — granice
odpowiedzialności opisuje [MOBILE_PORTABILITY.md](MOBILE_PORTABILITY.md).

## Najkrótsze demo

Po jednorazowej konfiguracji desktopowego presetu:

```sh
cmake --preset desktop-release
cmake --build --preset desktop-release --target tatra-demo
```

Target tworzy `out/t20/demo/result.json` i `out/t20/demo/overlay.svg` na małych,
wbudowanych danych syntetycznych. Samo wykonanie nie korzysta z sieci ani danych
Tatr. Pierwsza konfiguracja CMake może pobrać przypięte zależności; GDAL musi być
zainstalowany w systemie.

Prawdziwy pakiet przygotowuje się według [T10](T10_DATA.md), [T11](T11_DEM.md),
[T12](T12_PEAKS.md) i [T13](T13_DATASET.md). Następnie:

```sh
./build/desktop-release/tatra render \
  --dataset data/tatras-v1 --scene examples/tatras-v1/south-east.json \
  --out out/tatras-demo
```

## Artefakty do obejrzenia

- [synthetic-overlay.svg](../examples/artifacts/synthetic-overlay.svg) — wynik
  deterministycznego `tatra render`, bez prywatnego zdjęcia;
- [replay.gif](../examples/artifacts/replay.gif) — trzy klatki kontrolowanego obrotu
  od północy do wschodu; wygenerowane z wyników `tatra replay` skryptem
  [create_replay_gif.py](../tools/demo/create_replay_gif.py) (regeneracja wymaga Pillow);
- [quality-report-img9553.json](../examples/artifacts/quality-report-img9553.json) —
  publiczne podsumowanie jednej demonstracyjnej próby fotograficznej, bez zdjęcia;
- [raport T19](T19_PERFORMANCE.md) i
  [surowe porównanie](../benchmarks/results/img_9553.json) — czas, pamięć i zgodność.

Raport jakości jawnie rozdziela dwa punkty kalibracyjne od dwóch oceniających,
podaje missing, medianę nearest-rank, p95 i zakres w px/stopniach. Nie zalicza T18:
jest tylko jedno zdjęcie, intrinsics są szacowane, a stanowisko i wysokości nie są
niezależnie zweryfikowane. Punkt pod chmurą pozostaje osobno jako wykluczony.

## CI i warianty budowania

Workflow sprawdza:

- Linux: Debug, Release oraz ASan+UBSan;
- Windows: Debug i testy rdzenia/C ABI;
- macOS: preset `mobile-core` i testy rdzenia/C ABI;
- Android: cross-build `tatra_mobile` dla arm64-v8a, API 26;
- iOS: cross-build `tatra_mobile` dla urządzenia arm64.

Domyślne testy nie potrzebują zdjęć ani dużego pakietu. Test prawdziwego datasetu
jest lokalnym testem integracyjnym aktywowanym przez `TATRA_REAL_DATASET_DIR`.
`release` oraz `mobile-core` działają z `TATRA_BUILD_DESKTOP_IO=OFF`, więc nie
wymagają GDAL ani JSON. Publiczny nagłówek mobilny jest kompilowany osobno jako C.

## Decyzje techniczne

- Geometria używa `double`, bo ECEF operuje na wartościach rzędu milionów metrów,
  a odejmowanie bliskich pozycji w `float` traciłoby potrzebną precyzję.
- Raster używa `float`, co dla IMG_9553 ogranicza payload wysokości+maski do około
  44,5 MiB; interpolacja i dalsza geometria przechodzą do `double`.
- `Result<T>` i jawne statusy C ABI przekazują przyczynę błędu. Brak danych terenu
  nie jest wysokością zero, a błąd nie znika w wyjątku przekraczającym ABI.
- `PeakEngine` pożycza teren i katalog przez const referencje; `FrameResult` posiada
  swoje rekordy. Most mobilny opakowuje ten kontrakt i sam posiada skopiowane dane.
- Rdzeń nie zależy od mobilnego SDK. GPS, kamera, IMU, deklinacja, orientacja ekranu,
  loader zasobów i UI należą do adaptera aplikacji.
- C ABI ma jawny numer wersji, typy stanów `int32_t`, nieprzezroczysty uchwyt oraz
  bufory należące do wywołującego. Dzięki temu nie eksportuje ABI STL/Eigen.
- Biblioteka i GeographicLib są budowane z PIC, aby target można było połączyć
  z biblioteką JNI `.so`.

## Znane ograniczenia

- Silnik wymaga znanej pozycji, wysokości, orientacji i parametrów kamery; nie
  rozpoznaje samodzielnie dowolnego zdjęcia.
- Kamera jest modelem pinhole bez dystorsji obiektywu, rolling shutter i cyfrowej
  stabilizacji/cropu.
- Widoczność to dyskretne próbkowanie DEM. Model nie uwzględnia aktualnych drzew,
  budynków, chmur, śniegu ani obiektów blisko kamery; GLO-30 wygładza ostre turnie.
- Nie ma pełnej walidacji terenowej T18: wymagane są trzy zdjęcia z wiarygodnych
  stanowisk i niezależnymi punktami kontrolnymi.
- `tatra evaluate` w CLI pozostaje niedokończone. Biblioteczne metryki T17,
  testy i publiczny raport jakości działają, ale nie ma jeszcze adaptera JSON dla
  tego polecenia.
- Cross-build potwierdza kompilowalność wspólnej biblioteki. Integracja sensorów,
  JNI/Swift oraz test na fizycznym urządzeniu pozostają następnym etapem.

## Cztery odpowiedzi po zadaniu

1. Wejściem mobilnej funkcji tworzącej jest zwalidowana geometria rastra, maska
   i katalog; wynikiem jest uchwyt albo status z komunikatem. Wejściem `evaluate`
   jest klatka z pozycją, pozą, kamerą i opcjami; wynikiem są obserwacje i diagnostyka.
2. Most kopiuje dane i posiada je do `destroy`. Wywołujący posiada bufor obserwacji;
   wskaźniki `peak_id` i `peak_name` są ważne do zniszczenia uchwytu.
3. C test używa płaskiego rastra i północnego szczytu o ręcznie znanym położeniu
   w kadrze. Porównuje też własność kopii, mały bufor oraz błędny kwaternion.
   Dotychczasowe testy geometrii i referencyjne wyniki T19 nadal obowiązują.
4. Niespełnione założenie zwraca nazwany status i komunikat: błędny argument,
   za mały bufor, błąd rdzenia lub błąd wewnętrzny. Brak terenu i niepewna
   widoczność pozostają osobnymi stanami obserwacji.
