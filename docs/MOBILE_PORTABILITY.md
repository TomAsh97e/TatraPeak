# Przenośny rdzeń Android/iOS

TatraPeak udostępnia dwa poziomy integracji bez zależności od SDK telefonu:

- `TatraPeak::core` — publiczne API C++20;
- `TatraPeak::mobile` i [mobile.h](../include/tatra/mobile.h) — stabilna granica C ABI
  dla Objective-C/Swift i cienkiej warstwy JNI na Androidzie.

Oba targety korzystają tylko z Eigen i statycznego GeographicLib. Nie zależą od
GDAL, nlohmann/json, systemu plików, sieci, kamery, GUI ani SDK Android/iOS.
GDAL i narzędzia przygotowania danych pozostają na komputerze. Telefon dostaje
gotowy regionalny raster Float32, maskę oraz katalog szczytów.

```text
desktop: DEM + OSM -> tatra-prepare -> zasoby aplikacji
                                      |
telefon: zasoby -> dekoder platformy -> C ABI -> wyniki -> natywny overlay
                   GPS/IMU/kamera -----^                |
                   korekta osi/północy -----------------+
```

## Kontrakt C ABI

`tatra_mobile_engine_create` kopiuje raster, maskę, rekordy i napisy. Po jego
powrocie aplikacja może zwolnić bufory wejściowe. `tatra_mobile_engine_destroy`
zwalnia całość. Utworzenie dużego rastra chwilowo wymaga pamięci na wejście i kopię;
bufory źródłowe należy zwolnić od razu po sukcesie.

Przed każdą klatką aplikacja wypełnia `tatra_mobile_query`, a następnie wywołuje
`tatra_mobile_engine_evaluate`. `tatra_mobile_engine_peak_count` daje zawsze
wystarczający rozmiar bufora wynikowego. Gdy bufor jest za mały, funkcja niczego
częściowo nie zapisuje, zwraca `TATRA_MOBILE_BUFFER_TOO_SMALL` i wymagany rozmiar.

Wynikowe struktury są własnością wywołującego. Pola `peak_id` i `peak_name`
wskazują napisy silnika ważne do jego zniszczenia; skopiuj je, jeśli mają żyć
dłużej. Osobne pole opcjonalnej wysokości opisowej zapobiega wyświetlaniu
elipsoidalnej wysokości używanej przez geometrię jako wysokości n.p.m. Jedna
instancja silnika nie jest thread-safe. Można używać różnych instancji równolegle
albo synchronizować dostęp z zewnątrz. Żaden wyjątek C++ nie przekracza C ABI —
status i opcjonalny bufor komunikatu opisują błąd.

`TATRA_MOBILE_ABI_VERSION` oraz `tatra_mobile_abi_version()` pozwalają sprawdzić
zgodność wrappera. Publiczne typy stanów mają jawny rozmiar `int32_t`; liczności
używają natywnego `size_t`. Test integracyjny jest napisany jako C, nie C++:
[mobile_c_api_test.c](../tests/integration/mobile_c_api_test.c).

## Wspólny build bez warstwy desktopowej

Na komputerze ten sam wariant sprawdza preset:

```sh
cmake --preset mobile-core
cmake --build --preset mobile-core --parallel 2
ctest --preset mobile-core
```

Preset wyłącza CLI oraz `tatra_io`, ale buduje `tatra_core`, `tatra_mobile` i testy
syntetyczne. Pierwsza konfiguracja może pobrać przypięte źródła zależności; późniejszy
build i testy nie potrzebują sieci.

## Android NDK

Przykład dla urządzeń arm64 (podstaw ścieżkę do własnego NDK):

```sh
cmake -S . -B build/android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/android-ndk/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release \
  -DTATRA_BUILD_TESTS=OFF -DTATRA_BUILD_CLI=OFF \
  -DTATRA_BUILD_DESKTOP_IO=OFF -DTATRA_BUILD_MOBILE_BRIDGE=ON
cmake --build build/android-arm64 --target tatra_mobile --parallel 2
```

W projekcie aplikacji najprościej dodać repozytorium przez `add_subdirectory` i
podlinkować `TatraPeak::mobile` do małej biblioteki JNI. Kotlin nie wywołuje C ABI
bezpośrednio — JNI odpowiada wyłącznie za konwersję tablic/struktur i zarządzanie
uchwytem. Algorytmy i walidacja pozostają we wspólnym kodzie.

## iOS

Statyczne biblioteki dla urządzenia arm64:

```sh
cmake -S . -B build/ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DTATRA_BUILD_TESTS=OFF -DTATRA_BUILD_CLI=OFF \
  -DTATRA_BUILD_DESKTOP_IO=OFF -DTATRA_BUILD_MOBILE_BRIDGE=ON
cmake --build build/ios --config Release --target tatra_mobile --parallel 2
```

Nagłówek C może zostać wystawiony Swiftowi przez bridging header albo module map.
Docelowe XCFramework powinno połączyć wariant urządzenia i symulatora; jego
pakietowanie należy do projektu aplikacji, nie rdzenia obliczeniowego.

CI wykonuje testy hosta na Linuxie, Windowsie i macOS, cross-kompiluje
`tatra_mobile` dla Android arm64-v8a i iOS arm64 oraz uruchamia ASan/UBSan na Linuxie.

## Dane telefonu — odpowiedzialność adaptera

Warstwa platformowa musi dostarczyć:

- pozycję WGS84 i wysokość **elipsoidalną**, nie bezrefleksyjnie wysokość GPS/mapy;
- kwaternion `camera_from_enu` w kolejności `w,x,y,z`;
- heading względem północy geograficznej, z korektą deklinacji magnetycznej;
- mapowanie osi urządzenia do osi kamery i aktualnej orientacji ekranu;
- intrinsics właściwe dla obiektywu, cropu, stabilizacji i rozmiaru klatki;
- wspólną podstawę czasu pozy/orientacji i klatek;
- przetworzone zasoby regionu zgodne z wersją danych.

Biblioteka celowo nie zgaduje tych wartości. Błędna konwencja sensora może dać
liczbowo poprawny, ale wizualnie przesunięty overlay. Na początek należy nagrać
krótką sesję z telefonu i odtworzyć ją offline, zanim zostanie dodany tryb live.

## Co oznacza „portable” w tym repozytorium

Portable jest silnik i jego granica ABI: ten sam kod, te same testy, brak zależności
od systemu mobilnego i konfiguracje cross-build. Repozytorium nie zawiera jeszcze
aplikacji, kodu JNI/Swift, dostępu do sensorów, loadera zasobów telefonu ani UI.
To są cienkie adaptery następnego etapu; ich brak nie wymusza zmian w algorytmach.
