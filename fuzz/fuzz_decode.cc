// libFuzzer target: arbitrary bytes never make decode, decode_delta, or encoded_size read or
// write out of bounds or claim more bytes than given; encoded_size agrees with decode; and
// any 128 values round-trip through FOR and PFOR, and 64-bit values through pack and unpack
// at the width the input names.
#include "bitpack.hh"

#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	std::uint32_t out[bitpack::block_size];
	const std::size_t used = bitpack::decode(data, size, out);
	if (used > size) { std::abort(); }
	const std::size_t step = bitpack::encoded_size(data, size);
	if (step > size || (used && step != used)) { std::abort(); }
	if (bitpack::decode_delta(data, size, 1, out) > size) { std::abort(); }

	std::uint32_t in[bitpack::block_size] = {};
	std::memcpy(in, data, std::min(size, sizeof in));
	std::uint8_t buf[bitpack::max_encoded_bytes];
	for (int mode = 0; mode < 2; ++mode) {
		std::size_t n = mode ? bitpack::encode_pfor(in, buf) : bitpack::encode_for(in, buf);
		if (n > sizeof buf || bitpack::decode(buf, n, out) != n || std::memcmp(in, out, sizeof in) != 0) { std::abort(); }
	}
	if (size) {
		std::uint64_t wide[bitpack::block_size] = {}, back[bitpack::block_size];
		std::memcpy(wide, data + 1, std::min(size - 1, sizeof wide));
		const unsigned bits = data[0] % 65;
		const std::uint64_t mask = bits == 64 ? ~0ull : (1ull << bits) - 1;
		for (auto& v : wide) { v &= mask; }
		std::uint8_t packed[bitpack::packed_bytes(64)];
		bitpack::pack(wide, bits, packed);
		bitpack::unpack(packed, bits, back);
		if (std::memcmp(wide, back, sizeof wide) != 0 || bitpack::max_bits(wide) > bits) { std::abort(); }
	}
	return 0;
}
