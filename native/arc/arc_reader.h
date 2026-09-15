#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "arc_types.h"
#include "byte_reader.h"
#include "file_source.h"

namespace arcextract {

// Thrown when a block's `compressor` field names a codec this build cannot
// decode. Carries the raw compressor id so the caller can look it up in
// eu.lielu.arcextract.core.model.CompressionMethods for the right
// user-facing message (e.g. "Cette archive utilise SREP : support Android
// indisponible").
class UnsupportedCompressorError : public std::runtime_error {
public:
    explicit UnsupportedCompressorError(std::string compressorId)
        : std::runtime_error("unsupported compressor: " + compressorId), compressorId_(std::move(compressorId)) {}
    const std::string& compressorId() const { return compressorId_; }

private:
    std::string compressorId_;
};

// Reads a FreeArc "ArC" container's structure — footer, directory blocks,
// file listing — without decompressing file *contents*. This mirrors
// `ARCHIVE::read_structure` / `DIRECTORY_BLOCK` from the original Unarc
// source exactly (see docs/ANALYSIS.md §1bis and §6bis for the byte-level
// mapping), starting from the FOOTER at the end of the file rather than
// scanning forward from the start — the same strategy the original uses,
// and the only one that scales to a 19 GB file.
//
// Listing itself never needs a codec: block metadata (FOOTER, DIRECTORY)
// is only actually compressed with something other than "store" in
// practice for larger archives, in which case listing requires the same
// codec backend actual extraction would need — this class reports that
// via UnsupportedCompressorError rather than silently returning an empty
// listing.
class ArcReader {
public:
    explicit ArcReader(const std::string& path);
    explicit ArcReader(int fd);

    // Full recursive listing of every file/directory across every
    // DIRECTORY_BLOCK referenced by the footer.
    std::vector<ArcEntry> list();

private:
    BlockDescriptor findAndReadFooterLocalDescriptor();
    std::vector<BlockDescriptor> readFooterControlBlocks(const BlockDescriptor& footerLocalDescriptor);
    std::vector<uint8_t> decompressBlock(const BlockDescriptor& block);
    void readDirectoryBlockInto(const BlockDescriptor& dirBlockDescriptor, std::vector<ArcEntry>& out);

    FileSource file_;
};

} // namespace arcextract
