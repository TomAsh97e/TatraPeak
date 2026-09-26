# T17: metryki błędu reprojekcji

Metryki porównują ręczną adnotację `(peak_id, u, v)` z `PeakObservation` o tym
samym, jednoznacznym identyfikatorze. Błąd piksela jest odległością euklidesową.
Błąd kątowy powstaje przez zamianę obu pikseli na znormalizowane promienie
`K^-1[u,v,1]`, ograniczenie iloczynu skalarnego do `[-1,1]` i `acos`.

Mediana i p95 używają definicji nearest-rank. Dla pustego zbioru oba wyniki są
`nullopt`, a nie zerem. `ReprojectionMetrics::points` zachowuje wynik każdej
adnotacji i jej ewentualny błąd, co pozwala przejrzeć wszystkie wartości przy
małym zbiorze.

## Liczniki i mianownik

- `annotated_count`: wszystkie adnotacje wejściowe.
- `prediction_count`: wszystkie obserwacje silnika, także spoza obrazu i bez
  odpowiadającej adnotacji.
- `predictions_in_frame_count`: obserwacje ze stanem `InFrame`.
- `matched_count`: adnotacje posiadające dokładnie jedną predykcję o tym samym id,
  również gdy predykcja nie ma piksela.
- `evaluated_count`: dopasowania posiadające piksel i użyte do obliczenia błędu.
- `missing_count`: adnotacje niewyliczone z powodu braku predykcji, braku piksela
  albo niejednoznacznego id.
- `ambiguous_count`: adnotacje z powtórzonym id po stronie adnotacji lub predykcji;
  implementacja nie wybiera sztucznie najbliższego wyniku.

Predykcja `OutsideFrame` zachowuje obliczony piksel, więc jest oceniana i może mieć
duży błąd. `BehindNearPlane` nie ma piksela i pozostaje w `missing_count`. Ukrycie
tekstu podczas układania etykiet SVG nie zmienia metryk: źródłem predykcji jest
marker w `PeakObservation`, a nie widoczność napisu.

Funkcje numeryczne przyjmują skończone piksele oraz kamerę zwalidowaną wcześniej
przez pipeline. `nearestRankPercentile` dodatkowo zwraca `nullopt` dla pustych lub
niefinitywnych danych i percentyla spoza zakresu `[0,100]`.

## Testy

```sh
ctest --test-dir build/debug -R ReprojectionMetrics --output-on-failure
```

Testy kontrolują znane wyniki numeryczne, odporność `acos` na błąd zaokrąglenia,
brak dopasowanych id, pełny mianownik, predykcję poza obrazem i duplikaty id.
