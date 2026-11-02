// a*b+c rounded once (IEEE fusedMultiplyAdd, round to nearest) for the WebAssembly build.
//
// Stands in for Nebula's runtime/include/galaxy/fused_multiply_add.h when the web engine compiles Nebula's THP decoder
// (runtime/src/native_thp_video.cpp, unchanged): that header includes x86 intrinsics and detects FMA3 with CPUID,
// neither of which exists in WebAssembly, and wasm has no fused multiply-add instruction (std::fma is a soft-float
// routine there, far too slow for a movie frame). This file gives the same API with plain binary64 arithmetic:
//
//   - the product of two binary32 values is exact in binary64 (24 + 24 bits < 53);
//   - the sum's rounding error is computed exactly (TwoSum, Knuth);
//   - when the sum is inexact it is rounded to odd (moved one binary64 ulp towards the exact value if its last bit is
//     even), and narrowing that to binary32 rounds once, as the fused operation does (Boldo and Melquiond, "Emulation
//     of a FMA and correctly rounded sums: proved algorithms using rounding to odd", IEEE Trans. Computers 57(4),
//     2008: binary64's 53 bits are at least 2 x 24 + 2).
//
// GALAXY_FUSED_MULTIPLY_ADD_IS_BASE is 1, so the decoder takes its single "instruction" kernel, which calls
// fused_multiply_add. Checked against the C library's correctly rounded fmaf by web/tests/thp_video_test.cpp.
#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

#define GALAXY_FUSED_MULTIPLY_ADD_IS_BASE 1

namespace galaxy {

[[nodiscard]] inline float fused_multiply_add(float a, float b, float c) noexcept {
#if defined(__clang__)
#pragma clang fp contract(off)  // every product and sum below rounds on its own
#endif
    const double product = static_cast<double>(a) * static_cast<double>(b);  // exact
    const double addend = static_cast<double>(c);
    const double sum = product + addend;
    if (!std::isfinite(sum)) {
        return static_cast<float>(sum);
    }
    // TwoSum: sum + error == product + addend exactly
    const double addend_part = sum - product;
    const double product_part = sum - addend_part;
    const double error = (product - product_part) + (addend - addend_part);
    if (error == 0.0) {
        return static_cast<float>(sum);
    }
    std::uint64_t bits = std::bit_cast<std::uint64_t>(sum);
    if ((bits & 1u) == 0u) {
        // Round to odd: the neighbour of `sum` on the exact value's side (sum is not zero here: a zero sum is exact)
        const bool away_from_zero = (error > 0.0) == (sum > 0.0);
        bits = away_from_zero ? bits + 1u : bits - 1u;
    }
    return static_cast<float>(std::bit_cast<double>(bits));
}

// The names Nebula's decoder also uses: the same exact operation, and no FMA3 to detect.
[[nodiscard]] inline float fused_multiply_add_sse2(float a, float b, float c) noexcept {
    return fused_multiply_add(a, b, c);
}

[[nodiscard]] inline bool host_has_fma3() noexcept {
    return false;
}

}  // namespace galaxy
