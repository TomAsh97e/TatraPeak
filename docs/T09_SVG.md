# T09: wizualizacja SVG

`writeOverlaySvg` używa układu pikselowego kamery bez dodatkowego cropu. Opcjonalne tło
otrzymuje `x=0`, `y=0`, wymiary kamery oraz `preserveAspectRatio="none"`; wywołujący odpowiada
za dostarczenie obrazu o tych samych wymiarach i geometrii projekcji.

Marker zawsze pozostaje w obliczonym punkcie. Etykiety próbują czterech położeń przy markerze,
a następnie deterministycznie wolnych wierszy. Gdy żadna etykieta się nie mieści, ukrywany jest
tekst i łącznik, ale nie marker ani rekord JSON.

Tryb zwykły pokazuje `VisibleInModel`, `Uncertain` i `MissingTerrain`, a ukrywa
`OccludedInModel`. `debug_visibility=true` pokazuje także przesłonięte i nieocenione punkty.
