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
