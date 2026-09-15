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
// A control block decompressed cleanly but its contents do not match the CRC
// recorded for it. Distinct from ArcFormatError so the listing can tell this
// apart from a structural failure and decide what to do about it, rather than
// every caller having to treat "checksum disagrees" and "this is not an
// archive" as the same thing.
class BlockCrcMismatchError : public ArcFormatError {
public:
    explicit BlockCrcMismatchError(const std::string& message) : ArcFormatError(message) {}
};

class ArcReader {
public:
    explicit ArcReader(const std::string& path);
    explicit ArcReader(const std::vector<std::string>& paths);
    explicit ArcReader(int fd);
    explicit ArcReader(const std::vector<int>& fds);

    // Full recursive listing of every file/directory across every
    // DIRECTORY_BLOCK referenced by the footer.
    std::vector<ArcEntry> list();

private:
    BlockDescriptor findAndReadFooterLocalDescriptor();
    std::vector<BlockDescriptor> readFooterControlBlocks(const BlockDescriptor& footerLocalDescriptor);
    std::vector<uint8_t> decompressBlock(const BlockDescriptor& block);
    void readDirectoryBlockInto(const BlockDescriptor& dirBlockDescriptor, std::vector<ArcEntry>& out);

    std::vector<ArcEntry> listWithCurrentCrcPolicy();

    FileSource file_;
    // Set only by list(), and only after a strict attempt has already failed
    // on a checksum. See list() for why that fallback exists and what still
    // guards the result.
    bool ignoreBlockCrc_ = false;
};

// Lists a repack's sibling `.bin` files without assuming how they relate to
// each other, because both layouts occur in the wild:
//
//  - one archive split across volumes ("-v" in FreeArc is documented as
//    "split archive to volumes each of SIZE bytes"), where only the
//    concatenation has a footer, and
//  - several self-contained archives sitting side by side, which is what the
//    official Inno Setup integration shipped with FreeArc iterates over
//    (`Archives = '{src}\*.arc'`, "Extracts all found archives" — see
//    native/third_party/freearc/Unarc/InnoSetup/FreeArc_Example.iss).
//
// Tries the concatenation first, then falls back to reading each part as its
// own archive, merging whatever parses. Entry positions are always reported
// relative to the concatenated stream, so extraction reads the same ordered
// fd set either way. Throws only if no interpretation yields an archive, with
// the per-part diagnostics gathered along the way.
std::vector<ArcEntry> listArchiveParts(const std::vector<std::string>& paths);
std::vector<ArcEntry> listArchiveParts(const std::vector<int>& fds);

} // namespace arcextract
