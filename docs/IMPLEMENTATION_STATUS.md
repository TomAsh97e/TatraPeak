**Stan repozytorium: szkielet do realizacji MVP.**

| Zakres | Stan |
|---|---|
| T01: CMake, C++20, przypięte zależności, presety | Przygotowane |
| Konwersja stopni/radianów i testy startowe | Zaimplementowane |
| Typy i deklaracje API T02–T17 | Przygotowane |
| Pomoc i wersja CLI | Zaimplementowane |
| T02–T07: walidacja, geometria, teren, silnik | Do implementacji |
| T08–T14: pliki, SVG, dane i rzeczywiste sceny | Do implementacji |
| T15–T18: interpolacja, replay i ewaluacja | Do implementacji |
| T19: benchmarki | Zaplanowane; brak pozornych pomiarów placeholderów |
| T20: CI i finalne demo | Konfiguracja CI gotowa; demo wymaga algorytmów i danych |

TODO(Txx) i błędy NotImplemented wskazują zadania z [listy](MVP_TASKS.md).
Przejście testów startowych potwierdza spójność szkieletu, nie poprawność przyszłych
algorytmów ani dokładność rozpoznawania szczytów. Test szkieletu należy zastąpić
rzeczywistą sceną integracyjną w T07.

CI buduje rdzeń na Linuxie i Windowsie. Linux obejmuje Debug, Release i ASan/UBSan.
Adaptery desktopowe wymagają GDAL i mają osobny preset do lokalnego sprawdzenia.
