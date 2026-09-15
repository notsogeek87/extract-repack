#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace arcextract {

// Decodes a FreeArc "lzma:..." compressed block.
//
// FreeArc stores no LZMA properties header in the stream: lzma_decompress2()
// in native/third_party/freearc/Compression/LZMA2/C_LZMA.cpp takes lc/lp/pb
// and the dictionary size as plain arguments, filled in by parse_LZMA() from
// the method string recorded in the block descriptor (e.g. "lzma:mfbt4:d1m").
// So the props must be reconstructed from that string before decoding, and the
// compressed bytes are fed to the decoder as-is.
//
// Returns true and fills `out` with exactly `origSize` bytes on success.
// Returns false when this build has no LZMA decoder compiled in (see
// isLzmaSupported) or the stream does not decode.
bool lzmaDecode(const std::string& method, const std::vector<uint8_t>& in, uint64_t origSize,
                std::vector<uint8_t>& out);

// False when the FreeArc submodule providing the decoder sources was not
// checked out at build time, so callers can keep reporting the codec as
// unsupported instead of failing as if the archive were broken.
bool isLzmaSupported();

// Parameters parse_LZMA() would derive from a method string. Only the fields
// the decoder actually needs; the match finder and friends are encoder-side.
struct LzmaParams {
    uint32_t dictSize = 64u * 1024 * 1024; // LZMA_METHOD's own default
    int litContextBits = 3;                // lc
    int litPosBits = 0;                    // lp
    int posStateBits = 2;                  // pb
};

// Exposed for testing: mirrors parse_LZMA()'s handling of the parameters that
// affect decoding ("d<size>", "lc<n>", "lp<n>", "pb<n>", and a bare size).
LzmaParams parseLzmaMethod(const std::string& method);

} // namespace arcextract
