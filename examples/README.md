`scene.json` przedstawia wirtualnego obserwatora; nie jest zweryfikowaną fotografią.
Sceny rzeczywistego pakietu Tatr i polecenie uruchomienia znajdują się w
[`tatras-v1/`](tatras-v1/).

session/ zawiera syntetyczny obrót kamery od północy do wschodu w ciągu sekundy.
Kwaterniony mają nazwane współrzędne w, x, y, z i kierunek camera_from_enu.
Nie są to nagrania sensorów. Sesja ma wspólny czas w nanosekundach od zera.
Można ją odtworzyć poleceniem:

```sh
./build/desktop-debug/tatra replay \
  --dataset data/tatras-v1 --session examples/session --out out/replay
```

Generator i warianty z kontrolowanym błędem opisuje
[`T16_REPLAY.md`](../docs/T16_REPLAY.md).

Współrzędne i parametry aparatu prawdziwych zdjęć należy zweryfikować w T18.

Gotowe artefakty wersji demonstracyjnej znajdują się w [`artifacts/`](artifacts/):
statyczny SVG, krótki GIF replay oraz publiczny raport jakości IMG_9553 bez zdjęcia.
Ich pochodzenie i ograniczenia opisuje [`T20_RELEASE.md`](../docs/T20_RELEASE.md).
