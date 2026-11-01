# Changelog

All notable changes to `bitpack` are recorded here, newest first. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## 2026-11-01

### Changed

- `bitpack_bench` stops, naming the width, when the reference codec packs different bytes from
  `pack` instead of printing `no` in its `same` column and timing the row.

### Added

- `bench/unpack_alias.cc` (`bitpack_unpack_alias`) times `unpack` as the packed input's
  offset against the output moves through a page, to look for 4 KiB aliasing, and at width
  32 times a `memcpy` of the same bytes.
- `docs/perf/unpack-loads.md`: the input-load counts and per-width timings behind the NEON
  kernels, the SSE2 trial, and the variants that do not help.

### Not changed

- Width 32 `unpack` on SSE2 still copies row by row. A `memcpy` of the block is 1.17× to
  1.23× faster with Apple clang 17 and gcc 16 on the i9-9980HK and 1.20× with clang 12 on an
  EPYC 7763, and 0.54× to 0.57× with gcc 11 there, which expands the copy to `rep movsq`
  (`bench/unpack_alias.cc`, three runs each).

## 2026-10-31

### Changed

- The NEON `unpack` kernels load each input word once. The kernels read the packed
  block through `uint8_t`, which may alias the `uint32_t` output, so each row loaded
  the words it used and a word was loaded again after every store: 32 loads of the one
  word at width 1, 44 loads for 13 words at width 13, 62 for 31 words at width 31. A
  row advances by at most one word, so a kernel now keeps the current word in a
  register and loads the next as a row crosses into it: one load per word, at most two
  live, no stack traffic, at every width (1440 loads across widths 1 to 32 became 528).
  Packed bytes, decoded values and the API are unchanged.

  Apple M4 Pro, Apple clang 17 `-O3`, `bench/bench.cc`, integers per ns, ten paired runs
  against the previous kernels (the speedup is the median of the paired ratios):

  | Bits | before | after | speedup | simdcomp |
  |---:|---:|---:|---:|---:|
  | 1 | 23.47 | 27.98 | 1.20× | 27.21 |
  | 2 | 24.20 | 26.77 | 1.10× | 25.59 |
  | 4 | 23.30 | 24.60 | 1.05× | 24.25 |
  | 8 | 20.74 | 21.24 | 1.08× | 21.22 |
  | 10 | 17.80 | 20.49 | 1.24× | 20.10 |
  | 12 | 17.70 | 19.64 | 1.12× | 19.56 |
  | 16 | 16.48 | 19.00 | 1.10× | 18.07 |
  | 20 | 14.23 | 15.66 | 1.08× | 16.63 |
  | 24 | 13.75 | 14.76 | 1.06× | 14.78 |
  | 28 | 12.59 | 13.93 | 1.09× | 14.44 |
  | 31 | 11.79 | 13.39 | 1.13× | 13.39 |

  Over widths 1 to 31 the geometric mean is 1.11× with Apple clang 17 (rows 0.99× to
  1.24×) and 1.13× with gcc 16. Against simdcomp's `simdunpack` the mean over widths 1
  to 31 went from 0.88× to 1.00×. Where the loads were few the gain is small, because
  the 32 stores bound the kernel (width 1 is 1.20×); at the middle widths the unpack
  instruction count falls up to 37%.

  Width 5 is the one row that does not follow: 0.99× in the paired runs (1.06× best of
  ten) and 1.01× in the first bench build, but 0.92× in two later builds of the bench
  that contain the same width 5 kernel (19.1 to 20.2 integers per ns, against 21.1 to
  22.0 for the previous kernels and for the new kernel in the first build): the row
  moves with the layout of the binary, not with the kernel.

  `unpack_delta` is unchanged: its carry chain bounds it (3.4 integers per ns at every
  width on both libraries).
- `unpack` at width 32 on NEON copies the block with `memcpy`: at width 32 every packed
  word is its value. 12.99 to 21.25 integers per ns (1.64×; simdcomp copies at 12.99).
  `unpack` with `in == out` was never supported (it gives wrong values at every other
  width); at width 32 it happened to work and is now undefined.

### Added

- `test/differential.cc` and `test/bitpack_v0.hh`, a frozen copy of the previous
  kernels: `pack`, `unpack` and `unpack_delta` agree with it at every width on single-bit,
  maximum and complement patterns at every position, random blocks, and arbitrary
  bytes read as a packed block (`bitpack_differential`, and `bitpack_differential_portable`
  for the portable path). Four mutants of the row logic (never advancing, loading the
  wrong word, dropping the width 31 mask, a wrong first word) and three of the width 32
  copy fail it.
- `bench/asm_check.cc` and `bench/asm_count.py` count the vector loads from the input,
  the stores to the output and the stack accesses of each unpack kernel in the generated
  assembly.
- `BITPACK_BENCH_REFERENCE` builds on AArch64: it fetches lemire/simdcomp (BSD-3, pinned
  to d530177) and times `simdunpack` and `simdunpackd1` on the same packed bytes. x86-64
  keeps SIMD-BP128. The benchmark reference was SSE-only before.

### Not changed

- The SSE2 kernels keep their row-wise loads. A load-once SSE2 kernel (one word in a
  register) cut the loads to B at every width, and did not decode faster on an Intel
  i9-9980HK: `unpack` +1.5% to +1.8% (geometric mean over the widths, rows 0.99× to 1.05×)
  with Apple clang 17 and gcc 16, twelve paired runs each, and `unpack_delta` −0.5% to
  −1% with clang (rows 17 to 21 and 31 at 0.86× to 0.89×) and +1.3% with gcc. x86 issues two L1
  loads a cycle, and SSE2's two-operand shifts overwrite their input, so a word used by
  two rows is copied between registers where the old code loaded it twice: the
  instruction count did not fall (clang) or rose up to 13% (gcc).
- The NEON `unpack_delta` kernel keeps its row-wise loads. Loading once did not speed it
  up (1.00× with Apple clang) and cost gcc 16 2% to 3% at widths 2 to 4 and 8.
- A `vsliq_n_u32` for the spilling rows: the compilers already emit `shl` and `usra` for
  them, and the instruction count was equal below width 24.
- Width 32 `unpack` on SSE2 still copies row by row. SIMD-BP128 copies it 1.3× faster on
  the i9; a `memcpy` there was not measured.
