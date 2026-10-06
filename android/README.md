# Android — praca etapami

Projekt Kotlin osadza `TatraPeak::mobile` przez `libtatra_jni.so`. Publiczny C ABI
i algorytmy rdzenia pozostają bez zmian. APK obsługuje `arm64-v8a`, od API 26.
Nie zawiera GDAL ani nlohmann/json. Dane syntetyczne odpowiadają
`tests/integration/mobile_c_api_test.c`.

## Linux Mint: konfiguracja i build

Wymagane: JDK 17, CMake >= 3.28 na hoście, Ninja, kompilator C++ oraz Android SDK.
Przypięte wersje: Gradle 8.11.1 (wrapper z SHA-256), AGP 8.9.2, Kotlin 2.1.20,
SDK 35, NDK 27.2.12479018, Android CMake 3.31.6.
Zgodność AGP/Gradle: [dokumentacja Android](https://developer.android.com/build/releases/agp-8-9-0-release-notes).

Ustaw `JAVA_HOME` i `ANDROID_HOME` we własnym środowisku. Alternatywnie lokalny,
ignorowany `android/local.properties` może zawierać `sdk.dir=...`. Nie commituj
ścieżek SDK/NDK, kluczy ani cache. Narzędzia można zainstalować przez SDK Manager:

```sh
sdkmanager 'platforms;android-35' 'build-tools;35.0.0' 'platform-tools' \
  'ndk;27.2.12479018' 'cmake;3.31.6'
cd android
./gradlew test
./gradlew assembleDebug
```

Pierwszy build pobiera zależności. Uruchomienie aplikacji nie wymaga internetu.
`test` kompiluje ten sam adapter JNI dla hosta i uruchamia go na prawdziwej JVM
z `-Xcheck:jni`; nie zastępuje testów arm64. Testy wspólne są w `src/sharedTest`.

APK: `app/build/outputs/apk/debug/app-debug.apk`.

## Odbiór na telefonie — osobny, obowiązkowy krok

Po podłączeniu Galaxy S10e i autoryzacji debugowania USB:

```sh
adb devices -l
./gradlew connectedDebugAndroidTest
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n pl.tatrapeak/.ui.MainActivity
```

Wynik syntetyczny: `visible / Visible`, wysokość opisowa 999 m,
`InFrame`, u = 640 ± 0.001 px, `VisibleInModel`, jeden rekord katalogu/projekcji.
Brak urządzenia nie jest zaliczeniem odbioru. Powtarzanie create/close sprawdza
kontrakt własności; pomiar wycieków i pamięci na Androidzie wymaga profilera.

## JNI i własność

`NativeEngine` implementuje `Closeable`, synchronizuje evaluate/close i odrzuca
evaluate po close. Native rejestruje monotonnie rosnące identyfikatory zamiast
udostępniać wskaźniki. RAII niszczy silnik także przy błędzie alokacji wrappera.
JNI ma barierę wszystkich wyjątków C++; statusy C ABI mapuje na
`NativeEngineException`. Tekst datasetu przechodzi przez standardowe UTF-8
(`ByteArray`), nie zmodyfikowane UTF-8 JNI. NUL w napisach jest niedozwolony.
Wyniki posiada JVM i pozostają ważne po close. Bufor obserwacji C jest używany
ponownie. Obecny rejestr serializuje wywołania wszystkich instancji.

Płaskie tablice są prywatnym transportem JNI, nie formatem pakietu danych.
Format mobilnego pakietu jest opisany w
[ANDROID_DATA_FORMAT_PROPOSAL.md](../docs/ANDROID_DATA_FORMAT_PROPOSAL.md). Asset
tworzy zwalidowany desktopowy konwerter:

```sh
./build/desktop-debug/tatra-prepare mobile --dataset data/tatras-v1 \
  --out android/app/src/main/assets/datasets/tatras-v1.tpk
```

Plik `.tpk` jest generowany i ignorowany przez Git. Bez niego aplikacja jawnie
przechodzi na fixture syntetyczny; z nim sprawdza SHA-256 i cały format przed JNI.

## Diagnostyka kamery, orientacji i lokalizacji

`Włącz kamerę` prosi o CAMERA. `GPS` prosi jednocześnie o FINE/COARSE i uruchamia
bieżące aktualizacje GPS bez lastLocation. `Pozycja ręczna` przyjmuje WGS84
i wysokość elipsoidalną; zapisuje ją przy odtwarzaniu Activity po obrocie ekranu.
Są jawne powody odmowy użycia starych pomiarów (>15 s), błędu XY >30 m,
błędu Z >50 m, braku wysokości lub dokładności XY. Brak dokładności Z jest
pokazywany jako nieznana wartość. Dostęp do kamery/GPS/sensorów kończy się w onStop.

CameraX wybiera tylny obiektyw na podstawie ogniskowej i wymiarów sensora:
kandydat 20–35 mm ekwiwalentu, najbliższy 26 mm. To jawna heurystyka klasy
głównego obiektywu, nie identyfikator aparatu. Dla aparatu logicznego wymuszany jest
wybrany physical ID, zoom=1, EIS/OIS=OFF; UI pokazuje metadane wyniku żądań.
Fizyczny wybór 1×, crop i stabilizacja wymagają sprawdzenia na Galaxy S10e.
Różne active arrays aparatu logicznego/fizycznego blokują intrinsics.

Podgląd używa `FIT_CENTER`. Overlay kontrolny mapuje punkty cropu i jego środek
jawną macierzą `sensorToView * inverse(sensorToBuffer)`; rotację obrazu dostarcza
CameraX. Parametry pinhole są prezentowane dla bufora i po crop/rotation.
`LENS_INTRINSIC_CALIBRATION` jest preferowane, fallback używa fizycznej ogniskowej,
rozmiaru sensora i pixel array. Skew spoza modelu ABI jest odrzucany, dystorsja/ISP
pozostają jawnym przybliżeniem. Zobacz [konwencję orientacji](../docs/ANDROID_ORIENTATION.md).

Zgodność transformacji z fizycznym podglądem, montaż osi i dokładność lokalizacji
nie są potwierdzone przez testy JVM. Testy arm64 i profilowanie pamięci są gotowe
do przeprowadzenia na telefonie. Bieżące evaluate pracuje maksymalnie 8 razy/s,
poza UI, z backpressure i deterministycznym close. Dataset to nadal niewielka
scena w okolicach 0°N/0°E tylko wtedy, gdy brak wygenerowanego assetu `.tpk`.
Z pakietem Tatr overlay rysuje wyłącznie rekordy `InFrame`, wraz z nazwą,
wysokością opisową i rozróżnionym stanem widoczności. Przycisk nagrania zapisuje
do 10 s / 100 klatek w zgodnym z T16 katalogu `files/sessions/<czas>/`.
Katalog można pobrać z debug APK przez `adb exec-out run-as pl.tatrapeak ...`.
