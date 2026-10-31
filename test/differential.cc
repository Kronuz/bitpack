// The live unpack kernels against a frozen copy of the previous ones (bitpack_v0.hh).
// The packed layout and the decoded values must be identical for every width: valid
// packed blocks (exhaustive single-bit and boundary values at every position, plus random)
// and arbitrary bytes read as a packed block (the decoder masks, so both agree on those too).
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "bitpack.hh"
#include "bitpack_v0.hh"

#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "CHECK failed: %s (line %d, bits %u)\n", #__VA_ARGS__, __LINE__, bits); std::abort(); } } while (0)

using block = std::array<std::uint32_t, 128>;

static void compare(unsigned bits, const std::uint8_t* packed, std::uint32_t base) {
	block a{}, b{}, da{}, db{};
	bitpack::unpack(packed, bits, a.data());
	bitpack_v0::unpack(packed, bits, b.data());
	CHECK(a == b);
	bitpack::unpack_delta(packed, bits, base, da.data());
	bitpack_v0::unpack_delta(packed, bits, base, db.data());
	CHECK(da == db);
}

int main() {
	std::mt19937_64 rng(20261031);
	for (unsigned bits = 0; bits <= 32; ++bits) {
		const std::uint32_t max = bits == 0 ? 0 : bits == 32 ? ~0u : (1u << bits) - 1;
		std::vector<std::uint8_t> p(bitpack::packed_bytes(bits) + 1, 0xa5), q(p);
		auto round_trip = [&](const block& in) {
			bitpack::pack(in.data(), bits, p.data());
			bitpack_v0::pack(in.data(), bits, q.data());
			CHECK(p == q);  // packed bytes identical, guard byte untouched
			block out{};
			bitpack::unpack(p.data(), bits, out.data());
			CHECK(out == in);
			compare(bits, p.data(), std::uint32_t(rng()));
		};
		// Every position with the maximum, every single bit, and the complement pattern.
		for (unsigned pos = 0; pos < 128; ++pos) {
			block in{};
			in[pos] = max; round_trip(in);
			in.fill(max); in[pos] = 0; round_trip(in);
			for (unsigned k = 0; k < bits; ++k) { in.fill(0); in[pos] = 1u << k; round_trip(in); }
		}
		for (int r = 0; r < 300; ++r) {
			block in;
			for (auto& v : in) { v = std::uint32_t(rng()) & max; }
			round_trip(in);
		}
		// Arbitrary packed bytes.
		for (int r = 0; r < 300; ++r) {
			for (auto& byte : p) { byte = std::uint8_t(rng()); }
			compare(bits, p.data(), std::uint32_t(rng()));
		}
	}
	std::puts("differential OK");
}
