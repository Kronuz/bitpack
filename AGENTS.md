# AGENTS.md — bitpack

## Repo map

```
bitpack.hh         The library: layout, SSE2/NEON/portable pack and unpack, delta decode, FOR/PFOR codecs.
test/test.cc       Round trips, SIMD/scalar byte equality, delta, codecs. Built twice: SIMD and BITPACK_NO_SIMD.
bench/bench.cc     Decode throughput per width; optional SIMD-BP128 reference and byte-equality check.
CMakeLists.txt     INTERFACE target bitpack::bitpack; tests and bench when top-level.
docs/bitpack.d2     README diagram; render: d2 --layout elk --sketch --theme 302 docs/bitpack.d2 docs/bitpack.svg
```

## Invariants

- The on-disk layout is fixed. SIMD, NEON, and portable paths must produce identical
  bytes; the test compares them.
- No pointer to packed data is ever dereferenced as `std::uint32_t*`; packed data
  may be unaligned.
- Row helpers stay `BITPACK_INLINE`. Removing it halves decode speed.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake -S . -B asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" && cmake --build asan && ctest --test-dir asan
```

Run the NEON path with an AArch64 cross compiler and `qemu-aarch64 -cpu max`.
