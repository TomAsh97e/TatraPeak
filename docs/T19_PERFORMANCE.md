# T19: Cost of Analyzing IMG_9553

## Result and Decision

One small optimization was retained: four constant `GridTerrain` bounds/indices
are computed during construction instead of for every sample. `sampleHeight`
became **8.18%** faster at a cost of **32 bytes per raster**. Total `evaluate`
time is effectively unchanged: **0.2444 → 0.2446 ms**. No whole-engine speedup
was demonstrated, so a more complex cache, SIMD, or custom allocator is not
justified here.

Measurement date: 2026-09-28, seven alternating reference/candidate pairs on one
CPU. The [machine-readable report](../benchmarks/results/img_9553.json) contains
all timing samples, metadata, and comparisons. The [instructions](../benchmarks/README.md)
describe the build, package reproduction, execution, and result comparison.

## Environment and Input

- Intel Core i7-12700KF, 20 logical CPUs, affinity `[2]`; 32,678,400 KiB RAM
  (31.16 GiB according to `/proc/meminfo`). Linux Mint 22.1, Linux
  6.8.0-142-generic x86_64.
- GCC 13.3.0, C++20, Release `-O3 -DNDEBUG`; warnings
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`, without LTO or `-ffast-math`.
  No parallel builds/tests during measurement; turbo frequency was not locked.
- Reference: core from `cedc27cf0b863dc5606dbac5f96df234951902fa` + T19 runner.
  Candidate: the same core with four cached `double` values in
  `terrain.hpp`/`terrain.cpp`. Identical runner code in the compared executables.
  The same HEAD in metadata means the base of an uncommitted patch at measurement
  time, not an absence of differences.
- `morskie-oko-height-trial` package, 53 peaks, 2160×4321 raster;
  Copernicus GLO-30 (2021), EGM2008 2.5', OSM from 2026-09-25, and five explicit
  T18 corrections. Manifest SHA-256:
  `235eb73eb33483d1ac2a6c8163a327c44cdcd26d0a5293d5b1b47f679607f4c4`.
  DEM SHA-256: `943edd67c853bc1b8c5074e7c9b18c82620182f3111944419da11ec61c3c2a0c`.
  Catalog SHA-256: `0552e92438b7c5650cb065df8077044477bda90f5d3a215bb38d606dafd478fe`.
- IMG_9553 scene: 5712×4284, fx=fy=3960.5594 px, cx=2856, cy=2142;
  heading/pitch/roll 203.7370232° / 6.4704° / 1.237824°.
  Camera at 49.2011388889 N, 20.0712777778 E, h=1454.4077 m ellipsoidal.
- Range 50–10,000 m, near=0.01 m, visibility step 10 m, uncertainty band 0.5 m,
  observer guard 2 m, target guard 30 m, limit 10,000 samples.
  Frame: 17 peaks out of range, 36 projected, 1,588 visibility samples.
  The full input is stored in the fixture and report JSON; no photo decoding was used.

## Method and Measurements

Each run used 3 warm-up batches followed by 31 measurement batches. The base
parameter was `--iterations 20`. Workloads and operation counts per batch:

| Measurement | Input | Operations / batch |
|---|---|---:|
| project | all 53 catalog points in ENU, same order | 10,000 × 53 |
| sampleHeight | 64×64 grid around the observer, 0.0001° step | 100 × 4,096 |
| traceVisibility visible | control ray ~890 m above a flat DEM, 86 samples | 100 |
| traceVisibility blocked | ridge near the observer, blocker at 26.94 m, 3 samples | 10,000 |
| traceVisibility Mnich | real package/scene, 163 samples | 100 |
| evaluate | complete analysis of one IMG_9553 frame | 20 |

Loop and checksum-computation overhead is included in the time; storing the
checksum in `volatile` is outside the timer. Empty-loop cost was not subtracted.

In the table below, median means the median of the seven run medians. p95 was
calculated with nearest-rank across all 217 per-batch averages for each version,
normalized to one operation; it is not the p95 latency of individual calls.
A positive reduction means a shorter time.

| Measurement | Median before → after [ns] | p95 before → after [ns] | Median reduction |
|---|---:|---:|---:|
| project | 4.085 → 4.087 | 4.118 → 4.135 | −0.04% |
| sampleHeight | 12.266 → 11.263 | 12.330 → 11.371 | +8.18% |
| traceVisibility visible | 12,783.20 → 12,787.31 | 12,847.08 → 12,861.10 | −0.03% |
| traceVisibility blocked | 556.805 → 555.761 | 569.746 → 560.235 | +0.19% |
| traceVisibility Mnich | 24,061.91 → 24,103.63 | 24,214.25 → 27,451.28 | −0.17% |
| evaluate | 244,371.55 → 244,631.05 | 245,435.20 → 245,375.50 | −0.11% |

The `sampleHeight` improvement occurred in all seven pairs. Variations in the
other medians do not justify claiming a speedup; one Mnich run had a timing spike,
retained in the report and visible in p95. No slow samples were discarded. The
first exploratory pair suggested ~1.8% for `evaluate`, but later pairs did not
confirm it, so it is not used as a conclusion.

Data reading and validation are a separate measurement: the median of seven runs
was 170.808 ms before and 170.736 ms after. The file-system cache was not cleared.
Each function has its own warm-up counter. Data preparation, equivalence checking,
and JSON writing are outside the analysis sections; SVG is not generated.

## Memory and Equivalence

`sizeof(GridTerrain)`: **104 → 136 B**, or +32 B, with no extra allocation and no
per-pixel overhead. Float32 heights and the mask still occupy **46,666,800 B**
(44.50 MiB). Peak process RSS through the end of analysis: before
137,980–138,240 KiB (median 138,080), after 138,080–138,240 KiB (median 138,240).
The RSS difference does not measure the cache cost: it includes GDAL, memory
pages, the fixture, and JSON objects; the change's direct cost comes from `sizeof`.

The cache belongs to the immutable raster and is copied and moved with it. It
requires no invalidation or synchronization. North-up validation guarantees step
signs. The interpolation operation order, division, boundary rules, and NoData
behavior are unchanged.

Every pair passed exact JSON value comparison with no epsilon: 53 projections,
4,096 heights, three rays, the full frame result, and nine orientations ±0.4 rad
with deterministic timestamps. SHA-256 of the normalized semantic snapshot:
`1f65a374c3f2c66432c3708269897632e4abc8b385736152b927f38f2dc21538`.
Complete snapshots remain in local `out/t19/repeat-*/report.json` files; the
repository report stores the digest and raw measurements, not the private photo.

New independent terrain tests cover interpolation of a known plane, every edge,
`nextafter` beyond each boundary, NaN/Inf, copy/move, and zero-weight NoData.
The benchmark test checks repeatability and rejection of a changed reference or
invalid arguments; four Python tests verify report arithmetic and comparison checks.

Final builds and tests: Desktop Release **78/78**, Desktop Debug **77/77**
(including the real Tatras package), Release core without adapters **34/34**,
ASan/UBSan **34/34**. Sanitizers were run outside the sandbox because
LeakSanitizer did not work under ptrace restrictions; leak detection was not disabled.

This is a performance and computation-regression measurement, not a new T18
accuracy validation or a promise of similar timing on a phone. Calibration,
heights, and the photo overlay were not changed.

## Four Post-Task Answers

1. Input: a validated package and scene, or a deterministic synthetic variant,
   repetition counts, and an optional reference. Output: a report of timing,
   memory, configuration, and check values; comparison may fail.
2. Dataset owns the terrain and catalog and outlives the engine that uses const
   references. The bounds cache belongs to `GridTerrain`; point lists and reports
   belong to the runner.
3. The test plane's height is computed with an independent simple formula,
   visibility fixtures use controlled terrain, and production output was compared
   with the old executable. Statistics were checked against hand-calculated values.
4. An invalid package/scene/repetition count, write failure, or reference mismatch
   produces a message and a nonzero exit code. Missing coverage and NoData remain
   explicit states. The report discloses missing memory data and measurement limits.
