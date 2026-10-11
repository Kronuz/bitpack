/*
 * Copyright (c) 2026 Germán Méndez Bravo (Kronuz)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Bit packing for blocks of 128 unsigned 32-bit integers.
//
// Layout: integer j of a block lives in lane j % 4, row j / 4. Each lane packs its
// 32 rows into `bits` 32-bit words, and word w of the four lanes forms 128-bit
// word w. A block at width `bits` is exactly 16 * bits bytes, and one 4-lane
// register decodes integers 4r..4r+3 in order. The layout is fixed on disk; SSE2,
// NEON, and the scalar path all read and write the same bytes.

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

// Define BITPACK_NO_SIMD to use the portable path everywhere (tests do).
#if defined(BITPACK_NO_SIMD)
#elif defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#define BITPACK_SSE 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define BITPACK_NEON 1
#endif

#if defined(__GNUC__) || defined(__clang__)
#define BITPACK_INLINE __attribute__((always_inline)) inline
#else
#define BITPACK_INLINE inline
#endif

namespace bitpack {

inline constexpr std::size_t block_size = 128;

// Bytes of one packed block at the given width.
constexpr std::size_t packed_bytes(unsigned bits) noexcept { return 16 * std::size_t(bits); }

// Smallest width that holds every value of the block.
inline unsigned max_bits(const std::uint32_t* in) noexcept {
	std::uint32_t acc = 0;
	for (std::size_t i = 0; i < block_size; ++i) { acc |= in[i]; }
	return 32 - unsigned(std::countl_zero(acc));
}

namespace detail {

// Row r of a lane starts at bit r * B: word r*B/32, shift r*B%32, spilling into the next word.
template <unsigned B, unsigned R>
struct row {
	static constexpr unsigned start = R * B;
	static constexpr unsigned word = start / 32;
	static constexpr unsigned shift = start % 32;
	static constexpr bool spills = shift + B > 32;
};

template <unsigned B>
constexpr std::uint32_t low_mask = B == 32 ? ~0u : (1u << B) - 1;

BITPACK_INLINE std::uint32_t load32(const std::uint8_t* p) noexcept {
	std::uint32_t v;
	std::memcpy(&v, p, 4);
	return v;
}
BITPACK_INLINE void store32(std::uint8_t* p, std::uint32_t v) noexcept { std::memcpy(p, &v, 4); }

// Scalar reference: the same layout one lane at a time (little-endian hosts).
template <unsigned B>
inline void pack_scalar(const std::uint32_t* in, std::uint8_t* out) noexcept {
	if constexpr (B > 0) {
		std::memset(out, 0, packed_bytes(B));
		for (unsigned lane = 0; lane < 4; ++lane) {
			for (unsigned r = 0; r < 32; ++r) {
				std::uint32_t v = in[4 * r + lane] & low_mask<B>;
				unsigned start = r * B, w = start / 32, s = start % 32;
				std::uint8_t* word = out + 4 * (4 * w + lane);
				store32(word, load32(word) | v << s);
				if (s + B > 32) { store32(word + 16, load32(word + 16) | v >> (32 - s)); }
			}
		}
	}
}

template <unsigned B>
inline void unpack_scalar(const std::uint8_t* in, std::uint32_t* out) noexcept {
	for (unsigned lane = 0; lane < 4; ++lane) {
		for (unsigned r = 0; r < 32; ++r) {
			if constexpr (B == 0) {
				out[4 * r + lane] = 0;
			} else {
				unsigned start = r * B, w = start / 32, s = start % 32;
				const std::uint8_t* word = in + 4 * (4 * w + lane);
				std::uint32_t v = load32(word) >> s;
				if (s + B > 32) { v |= load32(word + 16) << (32 - s); }
				out[4 * r + lane] = v & low_mask<B>;
			}
		}
	}
}

#if BITPACK_SSE

template <unsigned B, unsigned R>
BITPACK_INLINE __m128i unpack_row(const __m128i* in, __m128i mask) noexcept {
	using row = detail::row<B, R>;
	__m128i v = _mm_srli_epi32(_mm_loadu_si128(in + row::word), int(row::shift));
	if constexpr (row::spills) { v = _mm_or_si128(v, _mm_slli_epi32(_mm_loadu_si128(in + row::word + 1), int(32 - row::shift))); }
	if constexpr (B < 32) { v = _mm_and_si128(v, mask); }
	return v;
}

template <unsigned B, unsigned... R>
BITPACK_INLINE void unpack_rows(const __m128i* in, __m128i* out, __m128i mask, std::integer_sequence<unsigned, R...>) noexcept {
	(_mm_storeu_si128(out + R, unpack_row<B, R>(in, mask)), ...);
}

template <unsigned B>
inline void unpack_simd(const std::uint8_t* in32, std::uint32_t* out32) noexcept {
	auto in = reinterpret_cast<const __m128i*>(in32);
	auto out = reinterpret_cast<__m128i*>(out32);
	if constexpr (B == 0) {
		for (unsigned r = 0; r < 32; ++r) { _mm_storeu_si128(out + r, _mm_setzero_si128()); }
	} else {
		unpack_rows<B>(in, out, _mm_set1_epi32(int(low_mask<B>)), std::make_integer_sequence<unsigned, 32>{});
	}
}

// Prefix sum of four lanes plus the running carry (the last value of the previous row).
BITPACK_INLINE __m128i prefix4(__m128i v, __m128i& carry) noexcept {
	v = _mm_add_epi32(v, _mm_slli_si128(v, 4));
	v = _mm_add_epi32(v, _mm_slli_si128(v, 8));
	v = _mm_add_epi32(v, carry);
	carry = _mm_shuffle_epi32(v, 0xff);
	return v;
}

template <unsigned B, unsigned... R>
BITPACK_INLINE void unpack_delta_rows(const __m128i* in, __m128i* out, __m128i mask, __m128i& carry, std::integer_sequence<unsigned, R...>) noexcept {
	(_mm_storeu_si128(out + R, prefix4(unpack_row<B, R>(in, mask), carry)), ...);
}

template <unsigned B>
inline void unpack_delta_simd(const std::uint8_t* in32, std::uint32_t base, std::uint32_t* out32) noexcept {
	auto in = reinterpret_cast<const __m128i*>(in32);
	auto out = reinterpret_cast<__m128i*>(out32);
	__m128i carry = _mm_set1_epi32(int(base));
	if constexpr (B == 0) {
		for (unsigned r = 0; r < 32; ++r) { _mm_storeu_si128(out + r, carry); }
	} else {
		unpack_delta_rows<B>(in, out, _mm_set1_epi32(int(low_mask<B>)), carry, std::make_integer_sequence<unsigned, 32>{});
	}
}

template <unsigned B, unsigned R>
BITPACK_INLINE void pack_row(const __m128i* in, __m128i* out, __m128i mask, __m128i& acc) noexcept {
	using row = detail::row<B, R>;
	__m128i v = _mm_loadu_si128(in + R);
	if constexpr (B < 32) { v = _mm_and_si128(v, mask); }
	acc = _mm_or_si128(acc, _mm_slli_epi32(v, int(row::shift)));
	if constexpr (row::shift + B >= 32) {
		_mm_storeu_si128(out + row::word, acc);
		acc = row::spills ? _mm_srli_epi32(v, int(32 - row::shift)) : _mm_setzero_si128();
	}
}

template <unsigned B, unsigned... R>
BITPACK_INLINE void pack_rows(const __m128i* in, __m128i* out, __m128i mask, __m128i& acc, std::integer_sequence<unsigned, R...>) noexcept {
	(pack_row<B, R>(in, out, mask, acc), ...);
}

template <unsigned B>
inline void pack_simd(const std::uint32_t* in32, std::uint8_t* out32) noexcept {
	if constexpr (B > 0) {
		auto in = reinterpret_cast<const __m128i*>(in32);
		auto out = reinterpret_cast<__m128i*>(out32);
		__m128i acc = _mm_setzero_si128();
		pack_rows<B>(in, out, _mm_set1_epi32(int(low_mask<B>)), acc, std::make_integer_sequence<unsigned, 32>{});
	}
}

#elif BITPACK_NEON

BITPACK_INLINE uint32x4_t load_u32x4(const std::uint8_t* p) noexcept { return vreinterpretq_u32_u8(vld1q_u8(p)); }
BITPACK_INLINE void store_u32x4(std::uint8_t* p, uint32x4_t v) noexcept { vst1q_u8(p, vreinterpretq_u8_u32(v)); }

template <unsigned B, unsigned R>
BITPACK_INLINE uint32x4_t unpack_row(const std::uint8_t* in, uint32x4_t mask) noexcept {
	using row = detail::row<B, R>;
	uint32x4_t w = load_u32x4(in + 16 * row::word);
	uint32x4_t v;
	if constexpr (row::shift == 0) { v = w; } else { v = vshrq_n_u32(w, row::shift); }
	if constexpr (row::spills) { v = vorrq_u32(v, vshlq_n_u32(load_u32x4(in + 16 * (row::word + 1)), 32 - row::shift)); }
	if constexpr (B < 32) { v = vandq_u32(v, mask); }
	return v;
}

template <unsigned B, unsigned... R>
BITPACK_INLINE void unpack_rows(const std::uint8_t* in, std::uint32_t* out, uint32x4_t mask, std::integer_sequence<unsigned, R...>) noexcept {
	(vst1q_u32(out + 4 * R, unpack_row<B, R>(in, mask)), ...);
}

template <unsigned B>
inline void unpack_simd(const std::uint8_t* in, std::uint32_t* out) noexcept {
	if constexpr (B == 0) {
		for (unsigned r = 0; r < 32; ++r) { vst1q_u32(out + 4 * r, vdupq_n_u32(0)); }
	} else {
		unpack_rows<B>(in, out, vdupq_n_u32(low_mask<B>), std::make_integer_sequence<unsigned, 32>{});
	}
}

BITPACK_INLINE uint32x4_t prefix4(uint32x4_t v, uint32x4_t& carry) noexcept {
	const uint32x4_t zero = vdupq_n_u32(0);
	v = vaddq_u32(v, vextq_u32(zero, v, 3));
	v = vaddq_u32(v, vextq_u32(zero, v, 2));
	v = vaddq_u32(v, carry);
	carry = vdupq_laneq_u32(v, 3);
	return v;
}

template <unsigned B, unsigned... R>
BITPACK_INLINE void unpack_delta_rows(const std::uint8_t* in, std::uint32_t* out, uint32x4_t mask, uint32x4_t& carry, std::integer_sequence<unsigned, R...>) noexcept {
	(vst1q_u32(out + 4 * R, prefix4(unpack_row<B, R>(in, mask), carry)), ...);
}

template <unsigned B>
inline void unpack_delta_simd(const std::uint8_t* in, std::uint32_t base, std::uint32_t* out) noexcept {
	uint32x4_t carry = vdupq_n_u32(base);
	if constexpr (B == 0) {
		for (unsigned r = 0; r < 32; ++r) { vst1q_u32(out + 4 * r, carry); }
	} else {
		unpack_delta_rows<B>(in, out, vdupq_n_u32(low_mask<B>), carry, std::make_integer_sequence<unsigned, 32>{});
	}
}

template <unsigned B, unsigned R>
BITPACK_INLINE void pack_row(const std::uint32_t* in, std::uint8_t* out, uint32x4_t mask, uint32x4_t& acc) noexcept {
	using row = detail::row<B, R>;
	uint32x4_t v = vld1q_u32(in + 4 * R);
	if constexpr (B < 32) { v = vandq_u32(v, mask); }
	if constexpr (row::shift == 0) { acc = vorrq_u32(acc, v); } else { acc = vorrq_u32(acc, vshlq_n_u32(v, row::shift)); }
	if constexpr (row::shift + B >= 32) {
		store_u32x4(out + 16 * row::word, acc);
		if constexpr (row::spills) { acc = vshrq_n_u32(v, 32 - row::shift); } else { acc = vdupq_n_u32(0); }
	}
}

template <unsigned B, unsigned... R>
BITPACK_INLINE void pack_rows(const std::uint32_t* in, std::uint8_t* out, uint32x4_t mask, uint32x4_t& acc, std::integer_sequence<unsigned, R...>) noexcept {
	(pack_row<B, R>(in, out, mask, acc), ...);
}

template <unsigned B>
inline void pack_simd(const std::uint32_t* in, std::uint8_t* out) noexcept {
	if constexpr (B > 0) {
		uint32x4_t acc = vdupq_n_u32(0);
		pack_rows<B>(in, out, vdupq_n_u32(low_mask<B>), acc, std::make_integer_sequence<unsigned, 32>{});
	}
}

#else

template <unsigned B>
inline void unpack_simd(const std::uint8_t* in, std::uint32_t* out) noexcept { unpack_scalar<B>(in, out); }

template <unsigned B>
inline void unpack_delta_simd(const std::uint8_t* in, std::uint32_t base, std::uint32_t* out) noexcept {
	unpack_scalar<B>(in, out);
	for (std::size_t i = 0; i < block_size; ++i) { out[i] = base += out[i]; }
}

template <unsigned B>
inline void pack_simd(const std::uint32_t* in, std::uint8_t* out) noexcept { pack_scalar<B>(in, out); }

#endif

template <template <unsigned> class F, typename Fn, unsigned... B>
constexpr auto make_table(std::integer_sequence<unsigned, B...>) noexcept {
	return std::array<Fn, sizeof...(B)>{F<B>::fn...};
}

template <unsigned B> struct pack_fn { static constexpr auto fn = &pack_simd<B>; };
template <unsigned B> struct unpack_fn { static constexpr auto fn = &unpack_simd<B>; };
template <unsigned B> struct unpack_delta_fn { static constexpr auto fn = &unpack_delta_simd<B>; };
template <unsigned B> struct pack_scalar_fn { static constexpr auto fn = &pack_scalar<B>; };
template <unsigned B> struct unpack_scalar_fn { static constexpr auto fn = &unpack_scalar<B>; };

using pack_t = void (*)(const std::uint32_t*, std::uint8_t*) noexcept;
using unpack_t = void (*)(const std::uint8_t*, std::uint32_t*) noexcept;
using unpack_delta_t = void (*)(const std::uint8_t*, std::uint32_t, std::uint32_t*) noexcept;

inline constexpr auto widths = std::make_integer_sequence<unsigned, 33>{};
inline constexpr auto pack_table = make_table<pack_fn, pack_t>(widths);
inline constexpr auto unpack_table = make_table<unpack_fn, unpack_t>(widths);
inline constexpr auto unpack_delta_table = make_table<unpack_delta_fn, unpack_delta_t>(widths);
inline constexpr auto pack_scalar_table = make_table<pack_scalar_fn, pack_t>(widths);
inline constexpr auto unpack_scalar_table = make_table<unpack_scalar_fn, unpack_t>(widths);

}  // namespace detail

// Packs 128 values (each must fit in `bits`) into packed_bytes(bits) bytes. `out` needs no alignment.
inline void pack(const std::uint32_t* in, unsigned bits, std::uint8_t* out) noexcept {
	detail::pack_table[bits](in, out);
}

// Unpacks 128 values packed at `bits`.
inline void unpack(const std::uint8_t* in, unsigned bits, std::uint32_t* out) noexcept {
	detail::unpack_table[bits](in, out);
}

// Unpacks 128 deltas and prefix-sums them onto `base`: out[i] = base + d[0] + ... + d[i].
inline void unpack_delta(const std::uint8_t* in, unsigned bits, std::uint32_t base, std::uint32_t* out) noexcept {
	detail::unpack_delta_table[bits](in, base, out);
}

// Writes d[i] = in[i] - in[i-1] (d[0] = in[0] - base) for a strictly or weakly increasing block.
inline void delta_encode(std::uint32_t base, const std::uint32_t* in, std::uint32_t* out) noexcept {
	for (std::size_t i = 0; i < block_size; ++i) {
		out[i] = in[i] - base;
		base = in[i];
	}
}

// Scalar implementations of the same layout, for testing and portable baselines.
inline void pack_scalar(const std::uint32_t* in, unsigned bits, std::uint8_t* out) noexcept {
	detail::pack_scalar_table[bits](in, out);
}
inline void unpack_scalar(const std::uint8_t* in, unsigned bits, std::uint32_t* out) noexcept {
	detail::unpack_scalar_table[bits](in, out);
}

// ---------------------------------------------------------------------------------------------
// Block codecs with a one-byte header.
//
// header bits 0-5: width (0..32); bit 6: constant block; bit 7: exceptions follow.
//   constant:   header, then the value as 4 little-endian bytes ("all ones" is the value 1).
//   FOR:        header, then packed_bytes(width).
//   PFOR:       header, exception count n (1 byte), packed low bits, then n positions (1 byte
//               each) and n high parts packed at their own width (1 byte width + bytes).
// Values are taken as given; callers delta-encode doc ids first.

inline constexpr std::uint8_t constant_flag = 0x40;
inline constexpr std::uint8_t exceptions_flag = 0x80;

// Upper bound on an encoded block's size.
inline constexpr std::size_t max_encoded_bytes = 1 + 1 + packed_bytes(32) + block_size + 1 + block_size * 4;

inline std::size_t encode_for(const std::uint32_t* in, std::uint8_t* out) noexcept {
	bool constant = true;
	for (std::size_t i = 1; i < block_size && constant; ++i) { constant = in[i] == in[0]; }
	if (constant) {
		out[0] = constant_flag;
		std::memcpy(out + 1, &in[0], 4);  // little-endian hosts only (x86-64, AArch64)
		return 5;
	}
	unsigned bits = max_bits(in);
	out[0] = std::uint8_t(bits);
	pack(in, bits, out + 1);
	return 1 + packed_bytes(bits);
}

// Chooses the width that minimizes the encoded size, storing values above it as exceptions.
inline std::size_t encode_pfor(const std::uint32_t* in, std::uint8_t* out) noexcept {
	std::array<unsigned, 33> counts{};  // counts[b]: values needing exactly b bits
	std::uint32_t first = in[0];
	bool constant = true;
	for (std::size_t i = 0; i < block_size; ++i) {
		++counts[32 - unsigned(std::countl_zero(in[i]))];
		constant = constant && in[i] == first;
	}
	if (constant) { return encode_for(in, out); }
	unsigned max = 32;
	while (counts[max] == 0) { --max; }
	// Exact encoded sizes: FOR is 1 + packed; PFOR adds a count byte, a width byte, and per
	// exception one position byte plus (max - b) high bits.
	unsigned best = max;
	std::size_t best_size = 1 + packed_bytes(max), above = 0;
	for (unsigned b = max; b-- > 0;) {
		above += counts[b + 1];
		std::size_t size = 3 + packed_bytes(b) + above + (above * (max - b) + 7) / 8;
		if (size < best_size) { best = b; best_size = size; }
	}
	if (best == max) { return encode_for(in, out); }

	std::array<std::uint32_t, block_size> low{};
	std::array<std::uint8_t, block_size> positions{};
	std::array<std::uint32_t, block_size> high{};
	std::size_t n = 0;
	const std::uint32_t mask = best == 32 ? ~0u : (1u << best) - 1;
	for (std::size_t i = 0; i < block_size; ++i) {
		low[i] = in[i] & mask;
		if (in[i] > mask) {
			positions[n] = std::uint8_t(i);
			high[n++] = in[i] >> best;
		}
	}
	std::uint8_t* p = out;
	*p++ = std::uint8_t(best | exceptions_flag);
	*p++ = std::uint8_t(n);
	pack(low.data(), best, p);
	p += packed_bytes(best);
	std::memcpy(p, positions.data(), n);
	p += n;
	// High parts: a scalar bit stream at their own width.
	std::uint32_t hb = 0;
	for (std::size_t i = 0; i < n; ++i) { hb |= high[i]; }
	unsigned high_bits = 32 - unsigned(std::countl_zero(hb));
	*p++ = std::uint8_t(high_bits);
	std::uint64_t acc = 0;
	unsigned filled = 0;
	for (std::size_t i = 0; i < n; ++i) {
		acc |= std::uint64_t(high[i]) << filled;
		filled += high_bits;
		while (filled >= 8) { *p++ = std::uint8_t(acc); acc >>= 8; filled -= 8; }
	}
	if (filled) { *p++ = std::uint8_t(acc); }
	return std::size_t(p - out);
}

// Decodes a block written by encode_for or encode_pfor; returns the bytes consumed.
inline std::size_t decode(const std::uint8_t* in, std::uint32_t* out) noexcept {
	const std::uint8_t header = in[0];
	if (header & constant_flag) {
		std::uint32_t v;
		std::memcpy(&v, in + 1, 4);
		for (std::size_t i = 0; i < block_size; ++i) { out[i] = v; }
		return 5;
	}
	const unsigned bits = header & 0x3f;
	if (!(header & exceptions_flag)) {
		unpack(in + 1, bits, out);
		return 1 + packed_bytes(bits);
	}
	const std::size_t n = in[1];
	const std::uint8_t* p = in + 2;
	unpack(p, bits, out);
	p += packed_bytes(bits);
	const std::uint8_t* positions = p;
	p += n;
	const unsigned high_bits = *p++;
	std::uint64_t acc = 0;
	unsigned filled = 0;
	const std::uint32_t high_mask = high_bits == 32 ? ~0u : (1u << high_bits) - 1;
	for (std::size_t i = 0; i < n; ++i) {
		while (filled < high_bits) { acc |= std::uint64_t(*p++) << filled; filled += 8; }
		out[positions[i]] |= std::uint32_t(acc & high_mask) << bits;
		acc >>= high_bits;
		filled -= high_bits;
	}
	return std::size_t(p - in);
}

// Decodes a block of deltas and prefix-sums them onto `base`.
inline std::size_t decode_delta(const std::uint8_t* in, std::uint32_t base, std::uint32_t* out) noexcept {
	const std::uint8_t header = in[0];
	if (!(header & (constant_flag | exceptions_flag))) {
		unpack_delta(in + 1, header & 0x3f, base, out);
		return 1 + packed_bytes(header & 0x3f);
	}
	std::size_t used = decode(in, out);
	for (std::size_t i = 0; i < block_size; ++i) { out[i] = base += out[i]; }
	return used;
}

}  // namespace bitpack
