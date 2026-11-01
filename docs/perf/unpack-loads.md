# Unpack input loads

How the `unpack` kernels read their input, what each choice measures, and what is not worth doing. The kernels are the NEON, SSE2 and portable paths of `bitpack.hh`; the numbers come from `bench/bench.cc` loops and from the assembly counter `bench/asm_count.py`.

## The input is read once per word on NEON

A block at width `B` is `B` 128-bit words. Row `r` of the unpack reads word `r*B/32` and, when the row spills over a word boundary, the next one. A row advances by at most one word, so a kernel holds the current word in a register and loads the next as a row crosses into it. That is one load per word, at most two words live, and no stack traffic at any width.

The straightforward form reads each word where a row uses it. The kernels read the packed block through `uint8_t`, which may alias the `uint32_t` output, so the compiler cannot keep a word in a register across the 32 stores and loads it again after each one. At width 1 that is 32 loads of the single word. Staging all `B` words in an array avoids the reloads but outgrows the 16-register file from width 15 (36 stack accesses at width 31), which is more memory traffic than the reloads it replaces.

`bench/asm_check.cc` instantiates every kernel and `bench/asm_count.py` counts, per width, the vector loads whose base is the input, the stores to the output and the stack accesses in the generated assembly. `loads == B` is the check that a kernel reads each word once. Apple clang 17 `-O3`, AArch64:

| Bits | loads | instructions | row-wise (integers/ns) | load-once | speedup | 10th to 90th percentile |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 32 → 1 | 128 → 81 | 23.47 | 27.98 | 1.20× | 1.19–1.24 |
| 2 | 32 → 2 | 126 → 81 | 24.20 | 26.77 | 1.10× | 1.07–1.28 |
| 3 | 34 → 3 | 130 → 86 | 23.63 | 25.50 | 1.07× | 1.04–1.24 |
| 4 | 32 → 4 | 122 → 79 | 23.30 | 24.60 | 1.05× | 1.03–1.07 |
| 5 | 36 → 5 | 132 → 90 | 22.54 | 23.87 | 0.99× | 0.87–1.05 |
| 6 | 36 → 6 | 130 → 90 | 21.44 | 22.89 | 1.04× | 0.87–1.08 |
| 7 | 38 → 7 | 134 → 95 | 20.15 | 21.23 | 1.06× | 1.01–1.22 |
| 8 | 32 → 8 | 114 → 75 | 20.74 | 21.24 | 1.08× | 1.05–1.10 |
| 9 | 40 → 9 | 136 → 99 | 18.89 | 20.60 | 1.11× | 1.07–1.23 |
| 10 | 40 → 10 | 134 → 100 | 17.80 | 20.49 | 1.24× | 1.13–1.33 |
| 11 | 42 → 11 | 138 → 106 | 17.56 | 20.18 | 1.15× | 1.13–1.38 |
| 12 | 40 → 12 | 130 → 98 | 17.70 | 19.64 | 1.12× | 1.11–1.43 |
| 13 | 44 → 13 | 140 → 108 | 16.78 | 18.73 | 1.14× | 1.08–1.17 |
| 14 | 44 → 14 | 138 → 106 | 17.09 | 18.64 | 1.08× | 1.04–1.13 |
| 15 | 46 → 15 | 142 → 110 | 15.82 | 17.70 | 1.12× | 1.08–1.15 |
| 16 | 32 → 16 | 98 → 67 | 16.48 | 19.00 | 1.10× | 1.02–1.17 |
| 17 | 48 → 17 | 144 → 114 | 14.87 | 17.25 | 1.20× | 1.13–1.22 |
| 18 | 48 → 18 | 142 → 114 | 14.77 | 15.89 | 1.09× | 1.05–1.14 |
| 19 | 50 → 19 | 146 → 120 | 13.89 | 16.54 | 1.18× | 1.13–1.22 |
| 20 | 48 → 20 | 138 → 114 | 14.23 | 15.66 | 1.08× | 1.05–1.12 |
| 21 | 52 → 21 | 148 → 126 | 13.67 | 15.59 | 1.10× | 1.05–1.16 |
| 22 | 52 → 22 | 146 → 126 | 13.44 | 15.68 | 1.13× | 1.09–1.14 |
| 23 | 54 → 23 | 150 → 132 | 13.19 | 15.09 | 1.14× | 1.10–1.16 |
| 24 | 48 → 24 | 129 → 121 | 13.75 | 14.76 | 1.06× | 1.03–1.08 |
| 25 | 56 → 25 | 151 → 138 | 12.65 | 14.71 | 1.14× | 1.13–1.16 |
| 26 | 56 → 26 | 149 → 139 | 12.65 | 14.40 | 1.14× | 1.12–1.15 |
| 27 | 58 → 27 | 153 → 144 | 12.48 | 14.32 | 1.14× | 1.12–1.15 |
| 28 | 56 → 28 | 145 → 141 | 12.59 | 13.93 | 1.09× | 1.07–1.12 |
| 29 | 60 → 29 | 155 → 150 | 12.10 | 14.18 | 1.17× | 1.12–1.18 |
| 30 | 60 → 30 | 153 → 151 | 11.99 | 13.99 | 1.15× | 1.13–1.18 |
| 31 | 62 → 31 | 157 → 156 | 11.79 | 13.39 | 1.13× | 1.12–1.15 |
| 32 | 32 → 0 | 65 → 5 | 13.07 | 21.13 | 1.61× | 1.59–1.63 |

*Row-wise* is the kernel that reads each word where a row uses it (`test/bitpack_v0.hh` holds its output); *load-once* is `bitpack.hh`. The loads and instructions columns count the `unpack` kernel for that width, row-wise to load-once. The throughput columns are the best of ten paired runs, and *speedup* is the median of the ten per-run ratios. Across widths 1 to 32 the row-wise kernels make 1,440 input loads and the load-once kernels 496 (width 32 is a copy, 0 loads).

The instruction count falls by 1% at width 31 and by 37% at width 1. The speedup does not follow it. At the low widths the 32 output stores bound the kernel, so width 1 gains 1.20× from a 37% cut, and the middle widths gain the most (1.24× at width 10).

Over widths 1 to 31 the geometric mean of the speedup is **1.11×** with Apple clang 17 (rows 0.99× to 1.24×) and **1.13×** with gcc 16 (rows 0.96× to 1.27×). Against simdcomp's `simdunpack` on the same bytes the load-once mean is 1.00× (clang 1.00 to 1.01, gcc 0.99); the row-wise mean is 0.88×. `unpack_delta` is the same kernel in both builds and reads 1.00× with both compilers, which is the noise floor of the comparison.

## Width 32 is a copy

At width 32 the packed block is the values. The NEON `unpack` copies it with `memcpy`: 21.13 against 13.07 integers per ns (1.61×), and simdcomp copies at 12.97. `unpack` with `in == out` is not supported at any width (it gives wrong values at every other width), and at width 32 it is undefined.

## The measurement

Apple M4 Pro (14 cores); Apple clang 17.0.0 and Homebrew gcc 16, `-O3 -DNDEBUG`. Each run decodes 256 blocks (32 Ki integers, cache resident) per width and keeps the best of five 30 ms trials, which is the loop of `bench/bench.cc`. Two builds of the library header are timed in alternating runs (ten each, the older build first on odd runs) on a machine kept idle for the run, and a row is the median of the per-run ratios.

A single run is not enough on a shared host. The best of nine 50 ms trials (`bench/bench.cc`) moved a row between 0.88× and 1.17× from one run of a binary to the next, and the median over paired runs ruled on rows the benchmark's own table could not. A row can also move with the layout of the binary: width 5 on arm64 reads 0.99× in the paired runs, 1.06× at its best of ten, and 0.92× in two later builds of `bench/bench.cc` that contain the same kernel.

To reproduce, build `bitpack_bench` from two checkouts of the library, alternate the two binaries ten times, and compare each row's ratio per pair.

## Input offset against the output

`bench/unpack_alias.cc` times `unpack` with the output page aligned and the packed input starting at each of the 64 cache-line offsets of a page, to look for 4 KiB aliasing between the input loads and the output stores.

Apple M4 Pro (NEON), Apple clang 17 `-O3`, `bitpack_unpack_alias 5 6 13 24 31 32`, two runs. Integers per ns over the 64 offsets (best of four 20 ms trials each):

| Bits | median | lowest | highest | highest / lowest |
|---:|---:|---:|---:|---:|
| 5 | 22.08, 22.03 | 21.19, 20.40 | 23.53, 23.60 | 1.11×, 1.16× |
| 6 | 21.44, 22.74 | 20.75, 20.88 | 22.77, 23.28 | 1.10×, 1.11× |
| 13 | 17.78, 18.69 | 17.18, 17.47 | 18.52, 19.44 | 1.08×, 1.11× |
| 24 | 15.20, 15.04 | 14.85, 14.13 | 15.61, 15.56 | 1.05×, 1.10× |
| 31 | 13.18, 13.09 | 12.77, 12.74 | 13.54, 13.32 | 1.06×, 1.05× |
| 32 | 21.56, 21.50 | 19.97, 20.61 | 22.26, 22.23 | 1.11×, 1.08× |

On the M4 the 5% to 16% spread has no shape. The best and the worst offsets of a width differ between the two runs (width 5 is fastest at offset 0 in the first run and slowest there in the second), and the first and second halves of the page average the same. The NEON kernels do not depend on the relative address of the input and the output. A width that reads below its neighbours in one build of `bench/bench.cc` (width 5 is 0.92× in two builds) is the layout of that binary, not an aliasing effect that a different input address would remove.

The Intel Core i9-9980HK (SSE2) with Apple clang 17 and gcc 16, the same command and two runs per compiler, shows the same: highest over lowest offset is 1.05× to 1.12× at widths 5 to 31 and 1.10× to 1.14× at width 32, the best and worst offsets move from run to run, and the two halves of the page average within 2% at widths 5 to 31. Width 32 reads 2% to 4% lower in the second half of the page in all four runs.

## SSE2 keeps the row-wise loads

A load-once SSE2 kernel (one word in a register) cuts the loads to `B` at every width. On an Intel Core i9-9980HK, Apple clang 17 and gcc 16, twelve paired runs each, it decodes no faster:

| | `unpack` | `unpack_delta` |
|---|---:|---:|
| Apple clang 17 | +1.8% (rows 0.99× to 1.05×) | −0.5% (rows 17 to 21 and 31 at 0.86× to 0.89×) |
| gcc 16 | +1.7% (rows 0.99× to 1.04×) | +1.4% |

(Geometric mean over widths 1 to 32 of the median paired ratio.) x86 issues two L1 loads per cycle, so the redundant loads are not the bottleneck there. SSE2's two-operand shifts overwrite their input, so a word that two rows use is copied between registers where the row-wise kernel loads it twice: the instruction count does not fall with clang and rises by up to 13% with gcc. The row-wise SSE2 kernels stay.

Per-width medians of the paired ratios for the load-once SSE2 kernel against the row-wise one:

| Bits | `unpack` clang | `unpack` gcc | `unpack_delta` clang | `unpack_delta` gcc |
|---:|---:|---:|---:|---:|
| 1 | 1.01× | 1.01× | 1.01× | 1.02× |
| 2 | 1.01× | 1.01× | 0.94× | 1.02× |
| 3 | 1.00× | 1.02× | 1.02× | 1.02× |
| 4 | 0.99× | 1.01× | 1.02× | 1.01× |
| 5 | 1.02× | 1.01× | 1.01× | 1.03× |
| 6 | 1.01× | 1.01× | 0.94× | 1.03× |
| 7 | 1.01× | 1.03× | 1.03× | 1.02× |
| 8 | 0.99× | 0.99× | 1.00× | 1.00× |
| 9 | 1.03× | 1.00× | 1.13× | 1.04× |
| 10 | 1.01× | 1.00× | 1.01× | 1.02× |
| 11 | 1.04× | 1.03× | 1.04× | 1.02× |
| 12 | 1.02× | 1.03× | 1.08× | 1.00× |
| 13 | 1.01× | 1.01× | 1.03× | 1.00× |
| 14 | 1.02× | 1.01× | 1.01× | 1.00× |
| 15 | 1.01× | 1.02× | 1.02× | 1.00× |
| 16 | 1.02× | 1.01× | 1.01× | 1.00× |
| 17 | 1.00× | 1.02× | 0.89× | 1.00× |
| 18 | 1.01× | 1.00× | 0.89× | 1.01× |
| 19 | 1.03× | 1.02× | 0.88× | 1.00× |
| 20 | 1.03× | 1.02× | 0.87× | 1.01× |
| 21 | 1.03× | 1.04× | 0.88× | 1.00× |
| 22 | 1.03× | 1.03× | 0.96× | 1.01× |
| 23 | 1.01× | 1.03× | 1.04× | 1.00× |
| 24 | 1.04× | 1.02× | 1.12× | 1.01× |
| 25 | 1.01× | 1.02× | 1.02× | 1.01× |
| 26 | 1.04× | 1.02× | 1.00× | 1.04× |
| 27 | 1.03× | 1.02× | 1.00× | 1.05× |
| 28 | 1.02× | 1.03× | 1.08× | 1.02× |
| 29 | 1.02× | 1.01× | 1.18× | 1.00× |
| 30 | 1.02× | 1.02× | 0.91× | 1.01× |
| 31 | 1.04× | 1.01× | 0.86× | 1.02× |
| 32 | 1.01× | 1.01× | 1.05× | 1.01× |

## Variants that do not help

- **NEON `unpack_delta` loading once.** 1.00× with Apple clang, and gcc 16 loses 2% to 3% at widths 2 to 4 and 8. The carry chain bounds `unpack_delta` at 3.4 integers per ns on the M4 Pro at every width, whatever it loads. The delta kernels keep their row-wise loads.
- **`vsliq_n_u32` for the rows that spill into the next word.** The compilers already emit `shl` and `usra` for them, and the instruction count is equal below width 24.
- **A staged array of all `B` words.** Spills on a 16-register file from width 15.

## SSE2 width 32

The SSE2 `unpack` copies a width 32 block row by row, and SIMD-BP128 copies it 1.3× faster on the i9 (8.21 against 10.73 integers per ns in the README table). The `memcpy` column of `bench/unpack_alias.cc` times a `memcpy` of the block's 512 bytes in the same loop as `unpack` at width 32, with the input at each of the 64 page offsets. Median over the offsets, integers per ns, three runs each:

| Machine, compiler | `unpack` | `memcpy` | memcpy / unpack |
|---|---:|---:|---:|
| Intel Core i9-9980HK, Apple clang 17 | 6.95, 7.38, 7.48 | 8.13, 9.02, 9.22 | 1.17×, 1.22×, 1.23× |
| Intel Core i9-9980HK, gcc 16 | 7.40, 7.25, 7.30 | 8.65, 8.49, 8.61 | 1.17×, 1.17×, 1.18× |
| AMD EPYC 7763 (shared Linux VM), clang 12 | 8.67, 8.64, 8.50 | 10.37, 10.36, 10.19 | 1.20×, 1.20×, 1.20× |
| AMD EPYC 7763 (shared Linux VM), gcc 11 | 8.63, 8.85, 8.63 | 4.89, 4.81, 4.82 | 0.57×, 0.54×, 0.56× |

A `memcpy` is 17% to 23% faster with clang and gcc 16, and 43% to 46% slower with gcc 11, which expands the constant-length copy inline to `rep movsq` (clang 12 calls the C library's `memcpy`). The change is three lines next to the zero-width branch of the SSE2 `unpack`:

```cpp
} else if constexpr (B == 32) {
	std::memcpy(out32, in32, packed_bytes(32));  // width 32 is the values themselves
}
```

It stays out of the library until a form of the copy wins on every toolchain the library builds with.
