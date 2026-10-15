# bitpack

[![CI](https://github.com/Kronuz/bitpack/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/bitpack/actions/workflows/ci.yml)

Bit packing for blocks of 128 unsigned 32-bit integers, for C++20. Header-only, no
dependencies. It is the integer codec under search-engine postings: doc-id deltas,
term frequencies, and positions.

## What it is

One header, `bitpack.hh`:

- `pack` / `unpack` a block of 128 integers at a fixed bit width (0 to 32). A block
  at width `b` takes exactly `16 * b` bytes.
- `unpack_delta` decodes 128 deltas and prefix-sums them onto a base in the same
  pass, which is how sorted doc ids come back.
- `encode_for`, `encode_pfor`, and `decode`: block codecs with a one-byte header.
  FOR packs every value at the block's maximum width. PFOR picks the width that
  minimizes the encoded size and stores the few larger values as exceptions. A
  block whose values are all equal (for example, all frequencies 1) encodes in
  5 bytes.
- `encoded_size` reads an encoded block's size from its headers without decoding
  it, for stepping over blocks laid out back to back.

SSE2 on x86-64 and NEON on AArch64 decode four integers per instruction; a portable
path covers everything else. All three read and write the same bytes.

## Usage

```cpp
#include "bitpack.hh"

std::uint32_t ids[128] = { /* increasing doc ids */ };
std::uint32_t deltas[128];
bitpack::delta_encode(/*base*/ 0, ids, deltas);

std::uint8_t buf[bitpack::max_encoded_bytes];
std::size_t size = bitpack::encode_pfor(deltas, buf);   // header + packed bits (+ exceptions)

std::uint32_t back[128];
bitpack::decode_delta(buf, size, /*base*/ 0, back);      // back == ids; 0 if malformed
```

Fixed-width calls skip the header:

```cpp
unsigned bits = bitpack::max_bits(deltas);
std::uint8_t packed[bitpack::packed_bytes(32)];
bitpack::pack(deltas, bits, packed);
bitpack::unpack_delta(packed, bits, 0, back);
```

## Layout

Integer `j` lives in lane `j % 4`, row `j / 4`. Each lane packs its 32 rows into
`b` 32-bit words, and word `w` of the four lanes is 128-bit word `w`. One 4-lane
register therefore decodes integers `4r .. 4r+3` in order, and the delta prefix sum
runs in the register. The bytes are identical to Lemire's SIMD-BP128 layout; the
benchmark checks this at every width.

## Performance

Decoding, in integers per nanosecond, on an Intel i9-9980HK (macOS, Apple clang
`-O3`, cache-resident blocks). The host was running other builds, so absolute
numbers are indicative; each row compares both codecs in the same run.

| Bits | `unpack` | `unpack_delta` | Scalar | SIMD-BP128 | SIMD-BP128/D1 |
|---:|---:|---:|---:|---:|---:|
| 1 | 9.37 | 4.82 | 3.48 | 9.21 | 4.94 |
| 4 | 8.21 | 4.55 | 1.42 | 8.08 | 4.10 |
| 8 | 7.43 | 4.22 | 1.22 | 7.54 | 4.11 |
| 12 | 7.27 | 4.31 | 0.85 | 7.43 | 4.16 |
| 16 | 8.07 | 4.55 | 3.37 | 8.04 | 4.62 |
| 20 | 7.74 | 4.37 | 0.85 | 8.09 | 3.81 |
| 32 | 6.71 | 5.04 | 3.17 | 7.94 | 6.76 |

Across all 32 widths, `unpack` averages 0.99× SIMD-BP128 and `unpack_delta` 1.02×
SIMD-BP128/D1. Reproduce with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBITPACK_BENCH_REFERENCE=ON
cmake --build build && build/bitpack_bench
```

`BITPACK_BENCH_REFERENCE` fetches lemire/SIMDCompressionAndIntersection
(Apache-2.0) for the benchmark only; the library never links it.

## Build and test

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

The tests round-trip every width at every position, require SIMD and scalar output
to be byte-identical, and exercise FOR, PFOR, constant blocks, and delta decoding.
`bitpack_portable_test` builds with `BITPACK_NO_SIMD` to cover the portable path.

## Limits

- Little-endian hosts only (x86-64, AArch64): the on-disk words are little-endian.
- Blocks are exactly 128 integers. Shorter tails belong to a variable-length
  integer codec such as [varint](https://github.com/Kronuz/varint).

## License

MIT. See [LICENSE](LICENSE).
