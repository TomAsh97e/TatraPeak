# Android TATRAPK v1 Package

Status: **approved and implemented on 2026-10-06**. It does not change
`tatras-v1`, the public C ABI, or the T16 session format.

One uncompressed, little-endian `tatras-v1.tpk` file. No GeoTIFF/JSON/GDAL in
the application's native runtime. The desktop reads the existing validated
package through the current adapter; Android verifies the file before creating
the engine.

## Header — 168 Bytes

| Offset | Type | Meaning |
|---:|---|---|
| 0 | 8 bytes | magic `TATRAPK\0` |
| 8 | UInt32 | version = 1 |
| 12 | UInt32 | header length = 168 |
| 16 | UInt64 | total file length |
| 24, 28 | UInt32 × 2 | rows, columns |
| 32, 40 | Float64 × 2 | latitude/longitude of the first cell center, WGS84 degrees |
| 48, 56 | Float64 × 2 | latitude/longitude steps, degrees, including negative values |
| 64 | UInt32 | peak count |
| 68 | UInt32 | reserved = 0 |
| 72 | UInt32 | UTF-8 dataset ID length |
| 76 | UInt32 | reserved = 0 |
| 80 | UInt64 | height-section length = rows × columns × 4 |
| 88 | UInt64 | mask length = rows × columns |
| 96 | UInt64 | peak-section length |
| 104 | 32 bytes | SHA-256 of the whole file with bytes 104–135 zeroed |
| 136 | 32 bytes | SHA-256 of the validated input tatras-v1 package manifest |

The hash detects corruption and identifies the exact data; it is not a digital
signature. The converter first runs the existing package validation for the
source files and hashes.

## Sections in Order

1. Dataset ID: exactly the specified number of UTF-8 bytes, without a NUL.
2. Float32 heights, row-major, in **WGS84 ellipsoidal metres**.
3. UInt8 mask, row-major, containing only 0 (missing data) or 255 (valid height).
4. Peak catalog: records with a fixed 48-byte prefix and variable text data.

A peak record contains the ID and name lengths (UInt32 × 2),
latitude/longitude/ellipsoidal geometry height (Float64 × 3), a display-elevation
flag (UInt8 0/1), 7 zeroed reserved bytes, the display elevation (Float64; zero
when the flag is 0), followed by the UTF-8 ID and UTF-8 name. The display
elevation is copied from the catalog, not derived from the geometry height. Do
not add a default “above sea level” label when the source data does not identify
the vertical reference.

## Validation and Memory

The loader first verifies the size and SHA-256 as a stream, then decodes the
file. Every multiplication and addition involving counts is checked for
overflow. It rejects unknown versions, nonzero reserved fields, extra trailing
bytes, truncated sections, invalid UTF-8, NUL characters, duplicate IDs, invalid
coordinates, and non-finite valid heights.

Explicit v1 limits: file ≤ 256 MiB, ≤ 40 million cells, ≤ 100,000 peaks,
≤ 4096 bytes per string; rows/columns ≥ 2. The limits protect the Galaxy S10e
from uncontrolled allocation, but peak memory use must still be measured on the
phone: the C ABI copies the raster, and the JNI adapter currently also prepares
intermediate buffers. The loader releases the input arrays after create. The
converter rejects packages that exceed the limits.

The asset is placed locally in `android/app/src/main/assets/datasets/` (the large
file is ignored by Git); the application distinguishes a missing package from a
corrupt one and still provides offline synthetic diagnostics.

```sh
cmake --preset desktop-debug
cmake --build --preset desktop-debug --target tatra-prepare
mkdir -p android/app/src/main/assets/datasets
./build/desktop-debug/tatra-prepare mobile \
  --dataset data/tatras-v1 \
  --out android/app/src/main/assets/datasets/tatras-v1.tpk
```
