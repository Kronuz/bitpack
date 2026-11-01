// `unpack` throughput as the address of the packed input moves against the output buffer.
//
// The kernels read the input through uint8_t and store through uint32_t, so a load whose
// address matches a recent store in its low 12 bits can be held back by the store (4 KiB
// aliasing). The output is page aligned here and the input starts at every 64-byte offset
// of a page in turn, so a row that depends on the relative address shows as a spread across
// offsets while a row that does not stays flat. At width 32 a second column times memcpy of
// the same bytes, which is what the packed block is at that width.
//
// usage: bitpack_unpack_alias [BITS...]   (default 5 13 31 32)
// output: "bits offset integers/ns" per offset, then one summary line per width.
#include "bitpack.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using clock_type = std::chrono::steady_clock;

// Best-of-trials integers per nanosecond for one pass over every block.
template <typename F>
static double ints_per_ns(F&& decode_all, std::size_t ints) {
	double best = 0;
	for (int trial = 0; trial < 4; ++trial) {
		std::size_t passes = 0;
		auto start = clock_type::now();
		double seconds = 0;
		do {
			decode_all();
			++passes;
			seconds = std::chrono::duration<double>(clock_type::now() - start).count();
		} while (seconds < 0.02);
		best = std::max(best, double(passes * ints) / (seconds * 1e9));
	}
	return best;
}

static std::uint8_t* page_aligned(std::vector<std::uint8_t>& raw) {
	auto base = reinterpret_cast<std::uintptr_t>(raw.data());
	return raw.data() + ((4096 - (base & 4095)) & 4095);
}

struct summary {
	double lowest = 1e300, highest = 0;
	std::vector<double> all;
	void add(double v) { lowest = std::min(lowest, v); highest = std::max(highest, v); all.push_back(v); }
	double median() { std::nth_element(all.begin(), all.begin() + long(all.size() / 2), all.end()); return all[all.size() / 2]; }
};

int main(int argc, char** argv) {
	constexpr std::size_t blocks = 256;
	std::vector<unsigned> widths;
	for (int a = 1; a < argc; ++a) { widths.push_back(unsigned(std::atoi(argv[a]))); }
	if (widths.empty()) { widths = {5, 13, 31, 32}; }

	std::mt19937_64 rng(7);
	std::vector<std::uint8_t> out_raw(blocks * bitpack::block_size * 4 + 8192);
	auto* out = reinterpret_cast<std::uint32_t*>(page_aligned(out_raw));
	volatile std::uint32_t sink = 0;

	for (unsigned bits : widths) {
		if (bits > 32) { continue; }
		const std::uint32_t mask = bits == 32 ? ~0u : (1u << bits) - 1;
		std::vector<std::uint32_t> values(blocks * bitpack::block_size);
		for (auto& v : values) { v = std::uint32_t(rng()) & mask; }
		const std::size_t stride = bitpack::packed_bytes(bits);
		std::vector<std::uint8_t> raw(blocks * stride + 8192);
		std::uint8_t* aligned = page_aligned(raw);
		summary unpack_rows, copy_rows;
		for (unsigned off = 0; off < 4096; off += 64) {
			std::uint8_t* packed = aligned + off;
			for (std::size_t b = 0; b < blocks; ++b) { bitpack::pack(values.data() + b * 128, bits, packed + b * stride); }
			const double r = ints_per_ns([&] {
				for (std::size_t b = 0; b < blocks; ++b) { bitpack::unpack(packed + b * stride, bits, out + b * 128); }
				sink = sink + out[7];
			}, blocks * bitpack::block_size);
			unpack_rows.add(r);
			std::printf("%u %u %.3f\n", bits, off, r);
			if (bits == 32) {
				copy_rows.add(ints_per_ns([&] {
					for (std::size_t b = 0; b < blocks; ++b) { std::memcpy(out + b * 128, packed + b * stride, stride); }
					sink = sink + out[7];
				}, blocks * bitpack::block_size));
			}
		}
		std::printf("width %2u: unpack median %.2f, lowest %.2f, highest %.2f integers/ns (spread %.2fx)", bits,
		            unpack_rows.median(), unpack_rows.lowest, unpack_rows.highest, unpack_rows.highest / unpack_rows.lowest);
		if (bits == 32) { std::printf("; memcpy median %.2f, lowest %.2f, highest %.2f", copy_rows.median(), copy_rows.lowest, copy_rows.highest); }
		std::printf("\n");
	}
}
