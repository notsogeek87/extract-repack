#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace arcextract {

// Variable-length unsigned integer encoding used throughout the FreeArc
// "ArC" container format (block types, sizes, offsets...).
//
// This is a from-scratch, bounds-safe reimplementation of the exact
// bit layout defined by `MEMORY_BUFFER::readInteger()` in FreeArc's
// original Unarc/ArcStructure.h (GPL-2.0, Bulat Ziganshin) — see
// docs/ANALYSIS.md §1bis for the verified byte table this follows. The
// original reads the field by reinterpret-casting up to 8 bytes past the
// field as a uint32_t/uint64_t and relies on 8 bytes of allocated padding
// after every buffer to make that safe; here we instead assemble the value
// byte-by-byte so it only ever touches bytes that actually exist in
// [data, data+size), works regardless of alignment, and does not depend on
// host endianness assumptions beyond "value bytes are little-endian",
// which is part of the format itself, not a portability shortcut.
//
// Encoding: the first byte's low bits are a unary run of 1-bits (0 to 7 of
// them) terminated by a 0-bit (or, for the maximal run of 8 one-bits,
// escapes to a fixed 9-byte raw form). The run length n (1..8) is the
// number of bytes the value occupies; the value itself is `raw >> n` where
// `raw` is the little-endian n-byte word. Equivalently the value always
// holds exactly 7*n bits.
struct VleResult {
    uint64_t value = 0;
    size_t bytesConsumed = 0;
    bool ok = false;
};

// Reads one VLE-encoded integer starting at data[offset]. Returns
// ok=false (without throwing) if there are not enough bytes remaining —
// callers decide whether that means "truncated/corrupt archive".
VleResult readVle(const uint8_t* data, size_t size, size_t offset);

// Encodes `value` using the same scheme, appending bytes to `out`. Used by
// the test fixture generator and by anything that ever needs to write this
// format back out (not currently exercised by extraction, which is
// read-only, but kept alongside the reader so both directions stay in
// sync and testable against each other).
void writeVle(uint64_t value, std::vector<uint8_t>& out);

} // namespace arcextract
