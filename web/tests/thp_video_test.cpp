// THP movie decoding for the web engine (web/host/thp_video.cpp).
//
// 1. web/host/compat/galaxy/fused_multiply_add.h against the C library's correctly rounded fmaf: random operands and
//    sums placed on the halfway point between two binary32 values (where a double rounding would go wrong).
// 2. Nebula's decoder (third_party/nebula/runtime/src/native_thp_video.cpp): a constant image decodes to 128, and
//    a checksum of 300 synthetic frames with random coefficients and quantizers. Built natively with Nebula's own header
//    and in WebAssembly with the compat header, the two checksums must be equal (web/tests/run_thp_video_test.sh).
// 3. smg_thp_frame_size: the SOF0 size of a frame, and its refusals.
// The frames are generated here: no game data.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "galaxy/fused_multiply_add.h"
#include "galaxy/native_thp_video.h"
#include "thp_video.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void test_fused_multiply_add() {
    std::mt19937_64 random(0x6d61'6464u);
    const auto any_float = [&random] {
        // Random sign, exponent in a range whose products stay normal, random significand
        const std::uint32_t sign = static_cast<std::uint32_t>(random() & 1u) << 31;
        const std::uint32_t exponent = static_cast<std::uint32_t>(127 - 40 + random() % 81) << 23;
        return std::bit_cast<float>(sign | exponent | static_cast<std::uint32_t>(random() & 0x7F'FFFFu));
    };
    std::uint64_t compared = 0, wrong = 0;
    std::uint64_t double_rounding_traps = 0;
    for (int i = 0; i < 3'000'000; ++i) {
        float a = any_float(), b = any_float();
        float c = any_float();
        if (i % 3 == 1) {
            // The case a binary64 sum gets wrong: the exact sum is just off a binary32 halfway point, by less than half a
            // binary64 ulp, so the binary64 sum IS the halfway point and narrowing it rounds to even, which can be the
            // wrong side. With h half of c's binary32 ulp and 15 <= k <= 23:
            //   a = h (1 + 2^-k), b = 1 - 2^-k, a b = h - h 2^-2k  (exact: a binary64 holds it)
            // so a b + c = (c + h) - h 2^-2k, just below the halfway point above c (h 2^-2k < 2^-54 |c|).
            const int k = 15 + static_cast<int>(random() % 9);
            const int exponent = static_cast<int>((std::bit_cast<std::uint32_t>(c) >> 23) & 0xFF) - 127;
            const float h = std::ldexp(1.0f, exponent - 24);
            a = h * (1.0f + std::ldexp(1.0f, -k));
            b = 1.0f - std::ldexp(1.0f, -k);
            if (c < 0) {
                a = -a;  // keep a b on c's side, towards larger magnitude
            }
            ++double_rounding_traps;
        } else if (i % 3 == 2) {
            c = -static_cast<float>(static_cast<double>(a) * static_cast<double>(b));  // cancellation
        }
        const float expected = std::fmaf(a, b, c);
        const float got = galaxy::fused_multiply_add_sse2(a, b, c);
        ++compared;
        if (std::bit_cast<std::uint32_t>(expected) != std::bit_cast<std::uint32_t>(got)) {
            if (++wrong <= 5) {
                std::printf("  fma(%a, %a, %a): fmaf %a, header %a\n", a, b, c, expected, got);
            }
        }
    }
    // Exact operations, zeros, infinities
    const float special[][3] = {{1.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {-0.0f, 1.0f, -0.0f}, {1e30f, 1e30f, 1.0f},
                                {INFINITY, 1.0f, 1.0f}, {3.0f, 0.5f, -1.5f}, {0x1.000002p0f, 0x1.000002p0f, -1.0f}};
    for (const auto& s : special) {
        ++compared;
        if (std::bit_cast<std::uint32_t>(std::fmaf(s[0], s[1], s[2])) !=
            std::bit_cast<std::uint32_t>(galaxy::fused_multiply_add_sse2(s[0], s[1], s[2]))) {
            ++wrong;
        }
    }
    // The trap really traps: a sum in binary64 narrowed to binary32 gets some of them wrong
    std::uint64_t naive_wrong = 0;
    {
        const float c = 0x1.000002p0f, h = 0x1p-24f;  // c's last bit is odd: the halfway point rounds up to even
        const float a = h * (1.0f + 0x1p-15f), b = 1.0f - 0x1p-15f;
        const float naive = static_cast<float>(static_cast<double>(a) * static_cast<double>(b) + static_cast<double>(c));
        naive_wrong = std::bit_cast<std::uint32_t>(naive) != std::bit_cast<std::uint32_t>(std::fmaf(a, b, c));
    }
    check(naive_wrong == 1, "the double-rounding case defeats a plain binary64 sum");
    std::printf("fused multiply-add: %llu cases (%llu on a double-rounding trap), %llu different from fmaf\n",
                static_cast<unsigned long long>(compared), static_cast<unsigned long long>(double_rounding_traps),
                static_cast<unsigned long long>(wrong));
    check(wrong == 0, "the header's fused multiply-add matches fmaf");
}

// A baseline 4:2:0 frame of 16x16 as THP stores them (entropy data without byte stuffing)
struct Frame {
    std::vector<std::uint8_t> bytes{0xFF, 0xD8};
    void segment(std::uint8_t marker, const std::vector<std::uint8_t>& body) {
        const auto length = body.size() + 2;
        bytes.insert(bytes.end(), {0xFF, marker, static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length)});
        bytes.insert(bytes.end(), body.begin(), body.end());
    }
    void headers(const std::vector<std::uint8_t>& quantization, std::uint8_t dc_size, std::uint8_t ac_size) {
        std::vector<std::uint8_t> table{0};
        table.insert(table.end(), quantization.begin(), quantization.end());
        segment(0xDB, table);
        segment(0xC0, {8, 0, 16, 0, 16, 3, 1, 0x22, 0, 2, 0x11, 0, 3, 0x11, 0});
        // DC table: one code "0" of category dc_size. AC table: "0" = end of block, "1" = run 0, size ac_size.
        std::vector<std::uint8_t> dc{0x00, 1};
        dc.resize(17, 0);
        dc.push_back(dc_size);
        std::vector<std::uint8_t> ac{0x10, 2};
        ac.resize(17, 0);
        ac.insert(ac.end(), {0x00, ac_size});
        segment(0xC4, dc);
        segment(0xC4, ac);
        segment(0xDA, {3, 1, 0, 2, 0, 3, 0, 0, 63, 0});
    }
};

std::uint64_t fnv(std::uint64_t hash, const std::vector<std::uint8_t>& data) {
    for (const auto byte : data) {
        hash = (hash ^ byte) * 0x100'0000'01B3ull;
    }
    return hash;
}

void test_decoder() {
    // Every coefficient zero: the planes are 128
    Frame flat;
    flat.headers(std::vector<std::uint8_t>(64, 1), 0, 0);
    flat.bytes.insert(flat.bytes.end(), {0x00, 0x00});
    std::vector<std::uint8_t> y(256), u(64), v(64);
    const bool decoded = galaxy::thp::decode_video(flat.bytes, 16, 16, y, u, v) == 0;
    bool all128 = decoded;
    for (const auto* plane : {&y, &u, &v}) {
        for (const auto p : *plane) all128 = all128 && p == 128;
    }
    check(all128, "a frame of zero coefficients decodes to 128");

    // Random frames: every block has a random DC and 63 random AC coefficients of 4 bits
    std::mt19937 random(0x7468'7032u);
    std::uint64_t checksum = 0xCBF2'9CE4'8422'2325ull;
    int ok = 0;
    for (int n = 0; n < 300; ++n) {
        Frame frame;
        std::vector<std::uint8_t> quantization(64);
        for (auto& q : quantization) q = static_cast<std::uint8_t>(1 + random() % 64);
        frame.headers(quantization, 4, 4);
        std::vector<bool> bits;
        const auto put = [&bits, &random](int count) {
            for (int i = 0; i < count; ++i) bits.push_back((random() & 1u) != 0u);
        };
        for (int block = 0; block < 6; ++block) {
            bits.push_back(false);  // DC code
            put(4);
            for (int coefficient = 1; coefficient < 64; ++coefficient) {
                bits.push_back(true);  // AC code: run 0, size 4
                put(4);
            }
        }
        for (std::size_t i = 0; i < bits.size(); i += 8) {
            std::uint8_t byte = 0;
            for (std::size_t j = 0; j < 8; ++j) byte = static_cast<std::uint8_t>(byte << 1 | (i + j < bits.size() && bits[i + j]));
            frame.bytes.push_back(byte);
        }
        if (galaxy::thp::decode_video(frame.bytes, 16, 16, y, u, v) == 0) {
            ++ok;
        }
        checksum = fnv(fnv(fnv(checksum, y), u), v);
    }
    check(ok == 300, "the 300 random frames decode");
    std::printf("decoder: %d of 300 random frames decoded, planes checksum %016llx\n", ok,
                static_cast<unsigned long long>(checksum));
}

void test_frame_size() {
    Frame frame;
    frame.headers(std::vector<std::uint8_t>(64, 1), 0, 0);
    frame.bytes.insert(frame.bytes.end(), {0x00, 0x00});
    std::uint16_t width = 0, height = 0;
    check(smg_thp_frame_size(frame.bytes.data(), static_cast<std::uint32_t>(frame.bytes.size()), &width, &height) &&
              width == 16 && height == 16, "the frame size comes from SOF0");
    check(!smg_thp_frame_size(frame.bytes.data(), 2, &width, &height), "a frame cut before SOF0 is refused");
    std::vector<std::uint8_t> not_jpeg(frame.bytes);
    not_jpeg[1] = 0xD9;
    check(!smg_thp_frame_size(not_jpeg.data(), static_cast<std::uint32_t>(not_jpeg.size()), &width, &height),
          "a frame that does not start with SOI is refused");
    // A segment length that points past the end
    std::vector<std::uint8_t> cut(frame.bytes.begin(), frame.bytes.begin() + 8);
    check(!smg_thp_frame_size(cut.data(), static_cast<std::uint32_t>(cut.size()), &width, &height),
          "a segment past the end is refused");
}

}  // namespace

int main() {
    test_fused_multiply_add();
    test_decoder();
    test_frame_size();
    std::printf(failures ? "thp_video_test: %d failures\n" : "thp_video_test: all passed\n", failures);
    return failures ? 1 : 0;
}
