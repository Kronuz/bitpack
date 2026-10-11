# bitpack architecture

## Layout

A block is 128 integers in 4 lanes of 32 rows. Lane `l` holds integers
`l, l+4, l+8, …`. Within a lane, row `r` occupies bits `[r*b, r*b + b)` of the
lane's bit stream, which is `b` 32-bit words. Word `w` of lanes 0–3 is stored as
one 16-byte group, so the block is `b` such groups.

```
 group 0: lane0.w0 lane1.w0 lane2.w0 lane3.w0
 group 1: lane0.w1 lane1.w1 lane2.w1 lane3.w1
 ...
```

Decoding row `r` is one shift of group `r*b/32`, an OR with a left shift of the next
group when the row crosses a word boundary, and a mask: the same three operations
on all four lanes at once. The row yields integers `4r .. 4r+3`, already in order.

## Code generation

Each width is a template instance. A fold over `std::integer_sequence<0..31>` emits
the 32 rows with every shift and word index a compile-time constant, so each width
compiles to straight-line code with immediate shifts. The row helpers are forced
inline; without that, compilers outline the fold and decoding runs at less than
half speed. Runtime dispatch is a 33-entry function table indexed by width.

## Delta decoding

`unpack_delta` adds a 4-lane prefix sum to each row (two shifted adds) plus the
carry, the last value of the previous row broadcast to all lanes. Only the carry add
and the broadcast are on the dependency chain between rows.

## Block codecs

The header byte holds the width (bits 0–5), a constant flag (bit 6), and an
exceptions flag (bit 7).

- **Constant** blocks store the value in 4 bytes. All-ones frequency blocks and
  blocks of equal deltas cost 5 bytes.
- **PFOR** counts how many values need each width, then evaluates every candidate
  width `b` below the maximum with the exact encoded size: the packed block, a count
  byte, one position byte per exception, a width byte, and the exceptions' high bits
  packed at their own width. It falls back to FOR when no width beats it. Decoding
  unpacks the low bits with the SIMD path, then ORs each exception's high part into
  its position.

## Pointers and alignment

Every buffer is bytes. SSE uses unaligned loads and stores, NEON loads bytes and
reinterprets lanes, and the portable path goes through `memcpy`, so a block can
start at any offset (after a header byte, inside a segment file, in an mmap).
