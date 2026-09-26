# T15: interpolacja orientacji na osi czasu

`PoseTimeline` przechowuje poprawne próbki orientacji w ściśle rosnącej kolejności
timestampów. `create` odrzuca pusty zbiór, czas ujemny, duplikaty, kolejność
niemonotoniczną oraz niepoprawne kwaterniony. Korzysta z tej samej polityki co
reszta silnika: norma odchylona od jedności najwyżej o 0,001 jest normalizowana,
a większy błąd, współczynnik niefinitywny lub kwaternion zerowy są odrzucane.

`sampleAt` wyszukuje prawą próbkę przez `std::lower_bound`. Dla dokładnego czasu
zwraca zapisaną orientację. Pomiędzy próbkami oblicza udział czasu w `double` i
wykonuje SLERP najkrótszą drogą; znak drugiego kwaternionu jest odwracany, gdy ich
iloczyn skalarny jest ujemny.

## Polityka czasu

- Nie ma ekstrapolacji. Czas poza pierwszą i ostatnią próbką daje
  `ErrorCode::TimeOutsideSession`.
- `max_gap` ogranicza cały odstęp między dwiema próbkami otaczającymi żądany czas.
- Odstęp równy `max_gap` jest dozwolony, większy daje
  `ErrorCode::TimeGapTooLarge`.
- Dokładny timestamp nie wymaga interpolacji, dlatego działa również dla
  `max_gap=0`.
- Ujemny `max_gap` daje `ErrorCode::InvalidInput`.
- Jedna próbka jest prawidłową osią czasu, ale obsługuje wyłącznie swój dokładny
  timestamp.

## Testy

Testy obejmują przejście heading `359° → 1°`, równoważne kwaterniony `q` i `-q`,
połowę obrotu `0° → 90°`, dokładny timestamp, jedną próbkę, pusty zbiór, czas
ujemny, duplikaty i złą kolejność, czas poza zakresem, zbyt dużą lukę oraz zerowy,
niefinitywny i źle znormalizowany kwaternion. Osobny test porównuje tę samą znaną
trajektorię zapisaną z dwiema i trzema próbkami; wyniki w czasach 250, 500 i 750 ms
są zgodne w tolerancji `1e-12` radiana.

Uruchomienie samych testów T15:

```sh
ctest --test-dir build/debug -R PoseTimeline --output-on-failure
```

## Cztery pytania kontrolne

1. Wejściem `create` jest wektor próbek czasu i orientacji, a wynikiem niezmienna
   logicznie oś czasu albo opisany błąd. `sampleAt` przyjmuje czas i limit luki,
   zwracając orientację albo błąd czasowy.
2. `PoseTimeline` przejmuje własność wektora i przechowuje znormalizowane kopie.
   Zwracany `CameraPose` również posiada własne dane.
3. Poprawność sprawdzają znane obroty, granice polityki czasu oraz porównanie tej
   samej trajektorii o dwóch częstotliwościach próbkowania.
4. Naruszenie kontraktu zwraca `InvalidInput`, `TimeOutsideSession` albo
   `TimeGapTooLarge`; żadna ścieżka nie używa po cichu ostatniej orientacji.
