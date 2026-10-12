// libFuzzer target: arbitrary bytes never make decode, decode_delta, or encoded_size read or
// write out of bounds or claim more bytes than given; encoded_size agrees with decode; and
// any 128 values round-trip through FOR and PFOR.
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
	return 0;
}
