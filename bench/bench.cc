// Decode throughput per width: bitpack's SIMD and scalar paths, and (with
// BITPACK_BENCH_REFERENCE) Lemire's SIMD-BP128 and SIMD-BP128/D1 on the same bytes.
#include "bitpack.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#if BITPACK_BENCH_REFERENCE
#include "simdbitpackinghelpers.h"
#include "deltatemplates.h"
#include "simdintegratedbitpacking.h"
#endif

using clock_type = std::chrono::steady_clock;

// Best-of-trials integers per nanosecond for decoding every block once per pass.
template <typename F>
static double ints_per_ns(F&& decode_all, std::size_t ints) {
	double best = 0;
	for (int trial = 0; trial < 9; ++trial) {
		std::size_t passes = 0;
		auto start = clock_type::now();
		double seconds = 0;
		do {
			decode_all();
			++passes;
			seconds = std::chrono::duration<double>(clock_type::now() - start).count();
		} while (seconds < 0.05);
		best = std::max(best, double(passes * ints) / (seconds * 1e9));
	}
	return best;
}

int main() {
	constexpr std::size_t blocks = 256;  // 32 Ki integers: decode-bound, cache-resident
	std::mt19937_64 rng(7);
	std::vector<std::uint32_t> out(blocks * bitpack::block_size);
	volatile std::uint32_t sink = 0;

	std::printf("%5s %10s %10s %10s", "bits", "simd", "simd+d1", "scalar");
#if BITPACK_BENCH_REFERENCE
	std::printf(" %10s %10s %8s", "ref", "ref+d1", "same");
#endif
	std::printf("   (integers per ns)\n");

	for (unsigned bits = 1; bits <= 32; ++bits) {
		std::uint32_t mask = bits == 32 ? ~0u : (1u << bits) - 1;
		std::vector<std::uint32_t> values(blocks * bitpack::block_size);
		for (auto& v : values) { v = std::uint32_t(rng()) & mask; }
		std::vector<std::uint8_t> packed(blocks * bitpack::packed_bytes(bits) + 16);
		for (std::size_t b = 0; b < blocks; ++b) {
			bitpack::pack(values.data() + b * 128, bits, packed.data() + b * bitpack::packed_bytes(bits));
		}
		auto stride = bitpack::packed_bytes(bits);
		auto simd = ints_per_ns([&] {
			for (std::size_t b = 0; b < blocks; ++b) { bitpack::unpack(packed.data() + b * stride, bits, out.data() + b * 128); }
			sink = sink + out[7];
		}, out.size());
		auto delta = ints_per_ns([&] {
			std::uint32_t base = 0;
			for (std::size_t b = 0; b < blocks; ++b) {
				bitpack::unpack_delta(packed.data() + b * stride, bits, base, out.data() + b * 128);
				base = out[b * 128 + 127];
			}
			sink = sink + out[7];
		}, out.size());
		auto scalar = ints_per_ns([&] {
			for (std::size_t b = 0; b < blocks; ++b) { bitpack::unpack_scalar(packed.data() + b * stride, bits, out.data() + b * 128); }
			sink = sink + out[7];
		}, out.size());
		std::printf("%5u %10.2f %10.2f %10.2f", bits, simd, delta, scalar);
#if BITPACK_BENCH_REFERENCE
		std::vector<std::uint8_t> theirs(packed.size());
		for (std::size_t b = 0; b < blocks; ++b) {
			SIMDCompressionLib::ArrayDispatch::SIMDpackwithoutmask(values.data() + b * 128, reinterpret_cast<__m128i*>(theirs.data() + b * stride), bits);
		}
		bool same = std::equal(packed.begin(), packed.end() - 16, theirs.begin());
		auto ref = ints_per_ns([&] {
			for (std::size_t b = 0; b < blocks; ++b) {
				SIMDCompressionLib::ArrayDispatch::SIMDunpack(reinterpret_cast<const __m128i*>(packed.data() + b * stride), out.data() + b * 128, bits);
			}
			sink = sink + out[7];
		}, out.size());
		auto ref_d1 = ints_per_ns([&] {
			__m128i init = _mm_setzero_si128();
			for (std::size_t b = 0; b < blocks; ++b) {
				init = SIMDCompressionLib::SIMDiunpack<SIMDCompressionLib::RegularDeltaSIMD>(
					init, reinterpret_cast<const __m128i*>(packed.data() + b * stride), out.data() + b * 128, bits);
			}
			sink = sink + out[7];
		}, out.size());
		std::printf(" %10.2f %10.2f %8s", ref, ref_d1, same ? "yes" : "no");
#endif
		std::printf("\n");
	}
}
