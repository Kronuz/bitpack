# AGENTS.md — bitpack

## Repo map

```
bitpack.hh         The library: layout, SSE2/NEON/portable pack and unpack, delta decode, FOR/PFOR codecs.
test/test.cc       Round trips, SIMD/scalar byte equality, delta, codecs. Built twice: SIMD and BITPACK_NO_SIMD.
test/differential.cc  Live kernels against test/bitpack_v0.hh, a frozen copy of the previous ones (pack, unpack, unpack_delta, every width).
bench/bench.cc     Decode throughput per width; optional SIMD-BP128 reference and byte-equality check.
bench/asm_check.cc, bench/asm_count.py   Counts the input loads and stack spills of each unpack kernel in the generated asm (not run).
CMakeLists.txt     INTERFACE target bitpack::bitpack; tests and bench when top-level.
docs/bitpack.d2     README diagram; render: d2 --layout elk --sketch --theme 302 docs/bitpack.d2 docs/bitpack.svg
```

## Invariants

- The on-disk layout is fixed. SIMD, NEON, and portable paths must produce identical
  bytes; the test compares them.
- No pointer to packed data is ever dereferenced as `std::uint32_t*`; packed data
  may be unaligned.
- Row helpers stay `BITPACK_INLINE`. Removing it halves decode speed.
- The NEON `unpack` kernels load each input word once: `bench/asm_count.py` on the
  `asm_check.cc` assembly shows `ld/B` equal to B and no stack traffic at every width.
  Do not read a word where a row uses it, and do not stage all B words in an array
  (ARCHITECTURE.md, "Code generation"). `unpack_delta` and the SSE2 kernels keep the
  row-wise loads.
- `test/bitpack_v0.hh` is frozen. A change to a kernel passes `bitpack_differential`
  against it, or the difference is deliberate and listed in the test.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake -S . -B asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" && cmake --build asan && ctest --test-dir asan
```

Run the NEON path with an AArch64 cross compiler and `qemu-aarch64 -cpu max`.
