// The in-place index writer (Aurora patch 0027, lib/gx/index_writer.hpp) against the appending one it replaced
// (prepare_idx_buffer, Aurora lib/gx/command_processor.cpp after patch 0025, MIT, Copyright (c) 2022 Luke Street; its
// logic copied below as the reference): the same indices, the same count, for every primitive, vertex count and start.
//   web/tests/run_index_writer_test.sh
#include "gx/index_writer.hpp"

#include <cstdio>
#include <vector>

using aurora::gx::fifo::idx_buffer_length;
using aurora::gx::fifo::write_idx_buffer;

namespace {

// prepare_idx_buffer, appending to a vector instead of Aurora's ByteBuffer
uint16_t reference(std::vector<uint16_t>& buf, GXPrimitive prim, uint16_t vtxStart, uint16_t vtxCount) {
  uint16_t numIndices = 0;
  if (prim == GX_QUADS) {
    for (uint16_t v = 0; v < vtxCount; v += 4) {
      uint16_t idx0 = vtxStart + v;
      uint16_t idx1 = vtxStart + v + 1;
      uint16_t idx2 = vtxStart + v + 2;
      uint16_t idx3 = vtxStart + v + 3;
      buf.insert(buf.end(), {idx0, idx1, idx2});
      numIndices += 3;
      buf.insert(buf.end(), {idx2, idx3, idx0});
      numIndices += 3;
    }
  } else if (prim == GX_TRIANGLES) {
    for (uint16_t v = 0; v < vtxCount; ++v) {
      buf.push_back(static_cast<uint16_t>(vtxStart + v));
      ++numIndices;
    }
  } else if (prim == GX_TRIANGLEFAN) {
    for (uint16_t v = 0; v < vtxCount; ++v) {
      const uint16_t idx = vtxStart + v;
      if (v < 3) {
        buf.push_back(idx);
        ++numIndices;
        continue;
      }
      buf.insert(buf.end(), {vtxStart, static_cast<uint16_t>(idx - 1), idx});
      numIndices += 3;
    }
  } else if (prim == GX_TRIANGLESTRIP) {
    for (uint16_t v = 0; v < vtxCount; ++v) {
      const uint16_t idx = vtxStart + v;
      if (v < 3) {
        buf.push_back(idx);
        ++numIndices;
        continue;
      }
      if ((v & 1) == 0) {
        buf.insert(buf.end(), {static_cast<uint16_t>(idx - 2), static_cast<uint16_t>(idx - 1), idx});
      } else {
        buf.insert(buf.end(), {static_cast<uint16_t>(idx - 1), static_cast<uint16_t>(idx - 2), idx});
      }
      numIndices += 3;
    }
  } else if (prim == GX_LINES || prim == GX_LINESTRIP || prim == GX_POINTS) {
    buf.insert(buf.end(), {0, 1, 3, 3, 2, 0});
    numIndices = 6;
  }
  return numIndices;
}

int failures = 0;
int cases = 0;

void check(GXPrimitive prim, uint16_t start, uint16_t count) {
  ++cases;
  std::vector<uint16_t> expected;
  const uint16_t expectedCount = reference(expected, prim, start, count);
  const uint32_t length = idx_buffer_length(prim, count);
  // A guard word after the indices: nothing is written past the length
  std::vector<uint16_t> out(length + 1, 0xBEEF);
  const uint16_t got = write_idx_buffer(out.data(), prim, start, count);
  const bool ok = length == expected.size() && got == expectedCount && out[length] == 0xBEEF &&
                  std::equal(expected.begin(), expected.end(), out.begin());
  if (!ok && failures++ < 10) {
    std::printf("FAIL prim 0x%02X start %u count %u: length %u (expected %zu), count %u (expected %u)\n",
                static_cast<unsigned>(prim), start, count, length, expected.size(), got, expectedCount);
  }
}

} // namespace

int main() {
  const GXPrimitive prims[] = {GX_QUADS, GX_TRIANGLES, GX_TRIANGLESTRIP, GX_TRIANGLEFAN,
                               GX_LINES, GX_LINESTRIP, GX_POINTS};
  // The starts merged draws use: 0, small, near the u16 end (indices wrap as before)
  const uint16_t starts[] = {0, 1, 2, 3, 4, 7, 100, 1000, 65000, 65533};
  for (const GXPrimitive prim : prims) {
    for (const uint16_t start : starts) {
      for (uint32_t count = 0; count <= 300; ++count) {
        check(prim, start, static_cast<uint16_t>(count));
      }
      // Large primitives (the count returned wraps at 65536 indices, as before)
      for (const uint16_t count : {4096, 21844, 21845, 21846, 30000, 65532}) {
        check(prim, start, count);
      }
    }
  }
  if (idx_buffer_length(static_cast<GXPrimitive>(0x00), 3) != UINT32_MAX) {
    std::printf("FAIL an unknown primitive has a length\n");
    ++failures;
  }
  std::printf("%s index writer: %d cases, %d failures\n", failures == 0 ? "PASS" : "FAIL", cases, failures);
  return failures == 0 ? 0 : 1;
}
