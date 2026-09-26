# Symulowane sceny T14

Wszystkie cztery główne sceny używają tego samego wirtualnego obserwatora:
`49.3000°N, 19.9800°E`, 2 m nad powierzchnią DEM. Nie jest to zweryfikowane
stanowisko fotograficzne. Kamera ma 1280 × 720 px i poziome pole widzenia 60°;
zmieniają się wyłącznie heading i timestamp.

Przykładowy widok z diagnostyką wybranych linii widzenia:

```sh
./build/desktop-debug/tatra render \
  --dataset data/tatras-v1 \
  --scene examples/tatras-v1/south-east.json \
  --out out/tatras-v1/south-east \
  --debug-peak osm:node:26862937 \
  --debug-peak osm:node:1862536196
```

`outside-coverage.json` celowo ustawia obserwatora tuż poza zachodnią krawędzią
rastra. Służy wyłącznie do kontroli, że brak pokrycia jest raportowany jawnie i nie
prowadzi do statusu `VisibleInModel`.
