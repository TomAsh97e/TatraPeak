# Konwencja adaptera orientacji Android

Kontrakt `mobile.h` pozostaje bez zmian: ENU (East, North, Up) → kamera
(prawo, dół, przód), kwaternion **w,x,y,z**. Nie zmieniamy obliczeń rdzenia.
Macierze są zapisane wierszami i mnożą wektory kolumnowe; iloczyn `A B p`
stosuje najpierw `B`. `Rz(a)=[[cos,-sin,0],[sin,cos,0],[0,0,1]]`.

`SensorManager.getRotationMatrixFromVector` zwraca `M = magnetic_ENU_from_device`.
Osie urządzenia są stałe względem naturalnej orientacji: x w prawo, y w górę
ekranu, z na zewnątrz ekranu. Dla tylnej kamery `B=diag(1,-1,-1)` daje kamerę
wyrównaną do naturalnego portretu. Bufor sensora wymaga obrotu CameraX.

Niech `S` = SENSOR_ORIENTATION, `r` = ImageInfo.rotationDegrees (obrót zgodny
z ruchem wskazówek zegara, uwzględniający target/display rotation), `d` = deklinacja
GeomagneticField, dodatnia na wschód. Wtedy:

```text
raw_camera_from_device = Rz(-S) B
upright_camera_from_true_ENU = Rz(r) Rz(-S) B transpose(M) Rz(d)
```

Uzasadnienie znaku deklinacji: true_ENU_from_magnetic_ENU = Rz(-d), więc
magnetyczna północ `(0,1,0)` dla dodatniego d ma dodatnią składową East.
Adapter potrzebuje transformacji odwrotnej, czyli Rz(d).

Przy standardowej tylnej kamerze `r = (S - displayDegrees + 360) % 360`.
Obrót ekranu nie jest stosowany ponownie do pozy: uwzględnia go r.
Intrinsics i wymiary dotyczą **tego samego przyciętego, obróconego bufora**.
Wynik C++ przechodzi do UI przez odwrotną macierz crop/rotation, potem
`sensorToView * inverse(sensorToBuffer)` z metadanych CameraX.

Próbki zachowują SensorEvent.timestamp. Bufor jest ograniczony do 256 wpisów;
dopasowanie wybiera najbliższą próbkę w granicy 25 ms i raportuje podpisaną
różnicę czasów. Nie dopisuje interpolacji ani geometrii z rdzenia w Kotlinie.
Gdy timestamp źródła kamery nie jest REALTIME, adapter odmawia łączenia zegarów
z sensorami. Brak magnetometru, niewiarygodna próbka, brak deklinacji albo
zbyt odległy czas daje jawny brak pozy, a nie poprzednią pozę używaną bezwarunkowo.

Testy mają niezależnie wpisane osie urządzenia dla północy, wschodu, zenitu
i landscape. W zenicie heading i roll są nieokreślone. Te testy sprawdzają
konwencję matematyczną, **nie potwierdzają montażu, kalibracji ani dokładności Galaxy S10e**.
Model nie uwzględnia drobnego odchylenia osi fizycznego obiektywu od korpusu.

Źródła: [SensorManager](https://developer.android.com/reference/android/hardware/SensorManager),
[ImageInfo](https://developer.android.com/reference/androidx/camera/core/ImageInfo),
[CameraCharacteristics](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics).
