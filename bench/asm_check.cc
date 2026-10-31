// Instantiates the unpack kernels of bitpack.hh for every width 1..32 as separate, non-inlined
// functions so the generated code can be counted: input loads against input words, stack spills.
// Compile with -S only; this is never run:
//
//   c++ -std=c++20 -O3 -I. -S bench/asm_check.cc -o /tmp/asm_check.s
//   python3 bench/asm_count.py /tmp/asm_check.s x86      # or arm64
//
// Each kernel should load exactly B words (the `ld/B` column) with no stack traffic.
#include "bitpack.hh"

#include <cstdint>
#include <utility>

template <unsigned B>
__attribute__((noinline)) void up(const std::uint8_t* in, std::uint32_t* out) noexcept { bitpack::detail::unpack_simd<B>(in, out); }
template <unsigned B>
__attribute__((noinline)) void ud(const std::uint8_t* in, std::uint32_t base, std::uint32_t* out) noexcept { bitpack::detail::unpack_delta_simd<B>(in, base, out); }

template <unsigned... B>
struct tables {
	static inline void (*const unpack[])(const std::uint8_t*, std::uint32_t*) noexcept = {&up<B + 1>...};
	static inline void (*const delta[])(const std::uint8_t*, std::uint32_t, std::uint32_t*) noexcept = {&ud<B + 1>...};
};
template <unsigned... B>
tables<B...> make(std::integer_sequence<unsigned, B...>);

using T = decltype(make(std::make_integer_sequence<unsigned, 32>{}));
// External linkage (a namespace-scope const would be internal and the kernels would be discarded).
extern void (*const* const keep_up)(const std::uint8_t*, std::uint32_t*) noexcept;
extern void (*const* const keep_ud)(const std::uint8_t*, std::uint32_t, std::uint32_t*) noexcept;
void (*const* const keep_up)(const std::uint8_t*, std::uint32_t*) noexcept = T::unpack;
void (*const* const keep_ud)(const std::uint8_t*, std::uint32_t, std::uint32_t*) noexcept = T::delta;
