// bitpack: round trips at every width and position, SIMD and scalar byte equality,
// delta decoding, and the FOR/PFOR block codecs.
#include "bitpack.hh"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); if (++failures > 20) std::exit(1); } \
	} while (0)

using block = std::array<std::uint32_t, bitpack::block_size>;

static block random_block(std::mt19937_64& rng, unsigned bits) {
	block b{};
	std::uint32_t mask = bits == 32 ? ~0u : (1u << bits) - 1;
	for (auto& v : b) { v = std::uint32_t(rng()) & mask; }
	return b;
}

int main() {
	std::mt19937_64 rng(20261011);
	static_assert(bitpack::packed_bytes(7) == 112);

	for (unsigned bits = 0; bits <= 32; ++bits) {
		for (int round = 0; round < 200; ++round) {
			block in = random_block(rng, bits);
			// Force the maximum value at a rotating position so every slot sees full width.
			if (bits) { in[round % 128] = bits == 32 ? ~0u : (1u << bits) - 1; }
			CHECK(bitpack::max_bits(in.data()) == bits);

			std::vector<std::uint8_t> simd(bitpack::packed_bytes(bits) + 1, 0xa5), scalar(bitpack::packed_bytes(bits) + 1, 0xa5);
			bitpack::pack(in.data(), bits, simd.data());
			bitpack::pack_scalar(in.data(), bits, scalar.data());
			CHECK(simd == scalar);  // same bytes, and the guard byte is untouched
			CHECK(simd.back() == 0xa5);

			block a{}, b{};
			bitpack::unpack(simd.data(), bits, a.data());
			bitpack::unpack_scalar(simd.data(), bits, b.data());
			CHECK(a == in);
			CHECK(b == in);

			std::uint32_t base = std::uint32_t(rng());
			block d{};
			bitpack::unpack_delta(simd.data(), bits, base, d.data());
			std::uint32_t sum = base;
			for (std::size_t i = 0; i < 128; ++i) { sum += in[i]; CHECK(d[i] == sum); }
		}
	}

	// delta_encode and decode_delta reproduce increasing doc ids.
	for (int round = 0; round < 500; ++round) {
		block ids{};
		std::uint32_t base = std::uint32_t(rng() % 1000), at = base;
		unsigned gap_bits = 1 + rng() % 20;
		for (auto& id : ids) { at += 1 + std::uint32_t(rng() % (1u << gap_bits)); id = at; }
		block deltas{}, back{};
		bitpack::delta_encode(base, ids.data(), deltas.data());
		std::vector<std::uint8_t> buf(bitpack::max_encoded_bytes);
		auto used = (round & 1) ? bitpack::encode_pfor(deltas.data(), buf.data()) : bitpack::encode_for(deltas.data(), buf.data());
		CHECK(bitpack::decode_delta(buf.data(), base, back.data()) == used);
		CHECK(back == ids);
	}

	// Block codecs: constant, all ones, skewed distributions with outliers.
	for (int round = 0; round < 5000; ++round) {
		block in{};
		switch (round % 5) {
		case 0: in.fill(std::uint32_t(rng())); break;
		case 1: in.fill(1); break;
		case 2: in = random_block(rng, rng() % 33); break;
		default: {
			in = random_block(rng, rng() % 6);
			unsigned outliers = rng() % 12;
			for (unsigned k = 0; k < outliers; ++k) { in[rng() % 128] = std::uint32_t(rng()) >> (rng() % 32); }
		}
		}
		std::vector<std::uint8_t> f(bitpack::max_encoded_bytes), p(bitpack::max_encoded_bytes);
		auto fn = bitpack::encode_for(in.data(), f.data());
		auto pn = bitpack::encode_pfor(in.data(), p.data());
		CHECK(pn <= fn);
		CHECK(pn <= bitpack::max_encoded_bytes);
		block a{}, b{};
		CHECK(bitpack::decode(f.data(), a.data()) == fn);
		CHECK(bitpack::decode(p.data(), b.data()) == pn);
		CHECK(a == in);
		CHECK(b == in);
		if (round % 5 == 1) { CHECK(fn == 5); }
	}

	std::printf("bitpack: %s\n", failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
