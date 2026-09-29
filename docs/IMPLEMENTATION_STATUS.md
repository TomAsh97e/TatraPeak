**Stan repozytorium: działający zakres T01–T17, T19–T20; T18 częściowo.**

| Zakres | Stan |
|---|---|
| T01: CMake, C++20, przypięte zależności, presety | Zaimplementowane |
| T02–T07: walidacja, geometria, teren, widoczność i silnik | Zaimplementowane |
| T08–T09: scena JSON, wynik i SVG | Zaimplementowane |
| T10–T13: dane, DEM, katalog i walidowany pakiet | Zaimplementowane |
| T14: symulowane sceny na rzeczywistym terenie | Zaimplementowane |
| T15: interpolacja orientacji na osi czasu | Zaimplementowane |
| T16: deterministyczny replay sesji | Zaimplementowane |
| T17: metryki błędu reprojekcji | Biblioteka i testy zaimplementowane; komenda CLI `tatra evaluate` pozostaje niedomknięta |
| T18: zestaw fotograficzny i raport jakości | Lokalne próby zdjęciowe i korekty wysokości; pełna walidacja niedomknięta |
| T19: benchmarki | Zaimplementowane; pomiar IMG_9553, zgodność z referencją, cache granic rastra (+32 B) |
| T20: CI, finalne demo i mobilna przenośność | Zaimplementowane: artefakty, jedno polecenie demo, C ABI, host CI oraz Android/iOS cross-build |

Test T14 potwierdza powtarzalność pipeline na konkretnym pakiecie, lecz pozycja
obserwatora pozostaje symulowana. Ocena dokładności wobec fotografii należy do T18.

CI buduje rdzeń na Linuxie, Windowsie i macOS. Linux obejmuje Debug, Release
i ASan/UBSan; osobne joby cross-kompilują most mobilny dla Androida i iOS.
Adaptery desktopowe wymagają GDAL i mają osobny preset do lokalnego sprawdzenia.

T19: [raport czasu i pamięci](T19_PERFORMANCE.md),
[uruchomienie benchmarków](../benchmarks/README.md).

T20: [wersja demonstracyjna](T20_RELEASE.md),
[integracja mobilna](MOBILE_PORTABILITY.md).
