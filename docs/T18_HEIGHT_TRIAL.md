# IMG_9553: Peak Height Correction Trial

Local artifacts: `out/t18/IMG_9553/`. The photo and data remain ignored by Git.
The trial does not complete T18 validation: it covers one photo, two calibration
points, two control points, and one summit partly obscured by a cloud.

User-provided viewpoint: 49.2011388889 N, 20.0712777778 E. The calculation uses
ground elevation 1,410 m above sea level + a phone height of 1.65 m, and the
EGM2008 geoid (N=42.7577 m), giving an ellipsoidal camera height of 1,454.4077 m.
The map elevation's vertical reference is unconfirmed. Original: 5712×4284,
iPhone 15 Pro, 1×, uncropped. Intrinsics were estimated from a 24 mm equivalent
focal length and kept fixed.

Local N values from `GeoidEval egm2008-2_5` were added to five OSM `ele` values.
This assumes the unspecified OSM datum is consistent with EGM2008. These heights
serve the experiment; they are not an independent, verified geodetic reference.
The corrections record their source, the input OSM export's SHA-256, and the
assumption. The new `dataset-height-corrected` package preserves the original
raster and coordinates.

| Peak | DEM ellipsoidal [m] | OSM + N [m] | Change [m] |
|---|---:|---:|---:|
| Wielki | 2322.21 | 2480.82 | +158.61 |
| Cubryna | 2338.43 | 2418.82 | +80.40 |
| Czarny | 2390.03 | 2452.81 | +62.78 |
| Pośredni | 2324.71 | 2431.82 | +107.10 |
| Mnich | 1986.24 | 2110.81 | +124.57 |

Heading/pitch/roll were fitted only to Cubryna and Mnich, retaining the previously
selected points marked in the photo and confirmed by the user. The comparison
uses the previous trial with the corrected observer height and unconstrained roll:

| Peak | Role | Before [px] | After [px] |
|---|---|---:|---:|
| Cubryna | calibration | 4.71 | 38.09 |
| Mnich | calibration | 4.19 | 34.15 |
| Wielki | control | 254.60 | 65.57 |
| Pośredni | control, approximate point | 193.36 | 92.68 |
| Czarny | clouds, excluded from control evaluation | 284.64 | 175.80 |

Errors refer to the original resolution. The fitted orientation is heading
203.737°, pitch 6.470°, roll 1.238° (previously roll 10.677°). All five points
have `VisibleInModel` status. This is an evaluation against the current DEM, not
proof of real-world visibility. The raster may still underestimate ridgelines.

The improvement at the control points supports the hypothesis that peak heights
have a significant effect. It does not prove that the DEM is the sole cause of
the error: position, annotation, focal-length, and height uncertainties remain.
The previously small errors at the two fitting points did not imply a correct
orientation across the whole photo.

`overlay.svg` contains the original photo, yellow confirmed annotations, and
cyan engine projections. `photo-trial-report.json` records the assumptions,
corrections, parameters, errors under the previous orientation, and errors after
refitting. The previous overlay image and report are retained in
`before-peak-heights/`.

## Second Trial: View from Rysy

The second trial used a 1824×1368 file without EXIF. The user provided a viewpoint
on Rysy and identified the camera as a Samsung Galaxy S23 using its 1× lens. The
official 85° field of view of the main camera was used. Because the summit from
which the photo was taken was unknown, the Slovak summit at 2,503 m and the Polish
summit at 2,499 m were calculated separately.

Gerlach and Wysoka were used as preliminary calibration points. Kończysta was
kept as an approximate control point. From the Slovak summit, the errors were
31 px for Gerlach, 28 px for Wysoka, and 63 px for Kończysta. From the Polish
summit, they were 41 px, 38 px, and 65 px respectively. The control-point
difference was less than 3 px, so the photo cannot determine which summit the
author stood on.

The second trial is not an independent validation: Gerlach is partly obscured by
a cloud, the identification of Kończysta requires confirmation, and the field of
view comes from the phone specification rather than EXIF. It nevertheless shows
that manual calibration is feasible for a file without metadata. Its control
point error is about 3.4% of the image width.

Photos, working scripts, complete JSON reports, and SVG overlays remain local in
the Git-ignored `out/t18/` directory. The repository stores the conclusions and
the tested correction mechanism without publishing private photos.
