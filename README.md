# bitpack

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/bitpack/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/bitpack/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only SIMD bit packing for C++20: 128-integer blocks with FOR and PFOR codecs,
byte-identical to SIMD-BP128. Integer `j` of a block lives in lane `j % 4`, so one 4-lane
register decodes four integers in order and the delta prefix sum runs in the register. It is
the integer codec under search-engine postings: doc-id deltas, term frequencies and
positions.

![bitpack: 128 integers in four lanes, packed at b bits into 16 × b bytes](docs/bitpack.svg)

## ✨ Highlights

- **Fixed-width blocks.** `pack` and `unpack` move a block of 128 integers at a width from 0
  to 32 bits; a block at width `b` takes exactly `16 * b` bytes.
- **Delta decoding in one pass.** `unpack_delta` decodes 128 deltas and prefix-sums them onto
  a base, which is how sorted doc ids come back.
- **FOR and PFOR codecs.** `encode_for`, `encode_pfor` and `decode` write and read a block
  with a one-byte header. PFOR picks the width that minimizes the encoded size and stores the
  few larger values as exceptions; a block of equal values takes 5 bytes.
- **Skippable.** `encoded_size` reads an encoded block's size from its headers without
  decoding it, for stepping over blocks laid out back to back.
- **Three paths, one format.** SSE2 on x86-64 and NEON on AArch64 decode four integers per
  instruction, a portable path covers the rest, and all three write the same bytes as
  Lemire's SIMD-BP128.
- **Safe decoding.** `decode` and `decode_delta` return 0 for a malformed or truncated block,
  on arbitrary input.
- **64-bit values.** `pack` and `unpack` also take blocks of `std::uint64_t`, as two 32-bit
  halves.

## 🚀 Quick start

```cpp
#include "bitpack.hh"

#include <cstdio>

int main() {
	std::uint32_t ids[128];
	for (std::uint32_t i = 0; i < 128; ++i) { ids[i] = 1000 + 3 * i; }   // increasing doc ids

	std::uint32_t deltas[128];
	bitpack::delta_encode(/*base*/ 0, ids, deltas);

	std::uint8_t buf[bitpack::max_encoded_bytes];
	std::size_t size = bitpack::encode_pfor(deltas, buf);   // header, packed bits, exceptions

	std::uint32_t back[128];
	std::size_t used = bitpack::decode_delta(buf, size, /*base*/ 0, back);   // 0 if malformed
	std::printf("%zu bytes, %zu read, last id %u\n", size, used, back[127]);
	// 37 bytes, 37 read, last id 1381
}
```

Fixed-width calls skip the header: `bitpack::pack(deltas, bits, packed)` and
`bitpack::unpack_delta(packed, bits, base, out)`, with `bits` from `bitpack::max_bits` and
`packed` sized by `bitpack::packed_bytes(bits)`.

## 📦 Installation

Requires a C++20 compiler, CMake 3.14 or newer, and a little-endian host (x86-64 or AArch64);
it has no dependencies. CI builds it on Linux (x86_64, arm64), macOS (arm64, x86_64), and
FreeBSD, plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(bitpack
  GIT_REPOSITORY https://github.com/Kronuz/bitpack.git
  GIT_TAG        ba5788c200b571b8a98631a33733eebafa18571a)  # main tracks the tip
FetchContent_MakeAvailable(bitpack)

target_link_libraries(my_app PRIVATE bitpack::bitpack)
```

Then `#include "bitpack.hh"`.

## 🎯 When to use it

Use it for postings, id lists and other runs of small integers that are decoded in hot loops.

Not for:

- Big-endian hosts: the packed words are little-endian on disk, and only x86-64 and AArch64
  are supported.
- Tails shorter than 128 integers: blocks are exactly 128, so a shorter tail belongs to a
  variable-length integer codec such as [varint](https://github.com/Kronuz/varint).

## ⚡ Performance

Decoding, in integers per nanosecond, with cache-resident blocks (`bench/bench.cc`, the best
of two runs, Apple clang `-O3`), against Lemire's reference at a pinned commit:
[simdcomp](https://github.com/lemire/simdcomp) on the Apple M4 Pro (NEON) and
[SIMD-BP128](https://github.com/lemire/SIMDCompressionAndIntersection) on the Intel Core
i9-9980HK (SSE2). Both hosts are shared with other work, so the absolute numbers are
indicative; each row compares the codecs in the same run.

| Bits | M4 Pro `unpack` | simdcomp | i9-9980HK `unpack` | SIMD-BP128 |
|---:|---:|---:|---:|---:|
| 1 | 27.46 | 25.84 | 9.86 | 9.85 |
| 8 | 21.23 | 21.22 | 9.13 | 9.30 |
| 16 | 17.39 | 17.56 | 8.79 | 8.85 |
| 24 | 15.08 | 14.23 | 8.26 | 8.47 |
| 32 | 21.25 | 12.97 | 8.21 | 10.73 |

Over widths 1 to 31, `unpack` averages 1.00 times simdcomp on the M4 Pro and 0.99 times
SIMD-BP128 on the i9-9980HK. `unpack_delta` is bound by its carry chain: 3.4 integers per ns
on the M4 Pro and 4.6 to 5.4 on the i9-9980HK, 1.01 and 1.03 times the references' delta
decoders. Width 32 is a copy, which the SSE2 kernel does row by row.
[docs/perf/unpack-loads.md](docs/perf/unpack-loads.md) has every width.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBITPACK_BENCH_REFERENCE=ON
cmake --build build
build/bitpack_bench
```

`BITPACK_BENCH_REFERENCE` fetches the reference for the benchmark only; the library never
links it.

## 📚 Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): the layout, the SIMD paths and the codecs.
- [docs/perf/unpack-loads.md](docs/perf/unpack-loads.md): how the unpack kernels read their
  input, per-width numbers, and the variants that do not help.
- [CHANGELOG.md](CHANGELOG.md): changes by date, with their measurements.
- [AGENTS.md](AGENTS.md): repository map and invariants.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#bitpack):
  include line, target and key API.

## 🛠️ Building and testing

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The tests round-trip every width at every position, require the SIMD and portable output to be
byte-identical (`bitpack_portable_test` builds with `BITPACK_NO_SIMD`), and compare the kernels
with a frozen copy at every width. `-DBITPACK_BENCH_REFERENCE=ON` adds the reference to the
benchmark, and `-DBITPACK_FUZZ=ON` builds the libFuzzer target.

## 🤝 Contributing

The encoded bytes are a storage format and must not change. Read [AGENTS.md](AGENTS.md) and
the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
before opening a pull request; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo. See [LICENSE](LICENSE).
