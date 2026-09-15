#include "arc_reader.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "byte_reader.h"
#include "crc32.h"
#include "lzma_decoder.h"

namespace arcextract {

namespace {

bool matchesSignatureAt(const std::vector<uint8_t>& buf, size_t i) {
    return buf[i] == kSignature[0] && buf[i + 1] == kSignature[1] &&
           buf[i + 2] == kSignature[2] && buf[i + 3] == kSignature[3];
}

std::string toHex(const std::vector<uint8_t>& buf, size_t count) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    const size_t n = std::min(count, buf.size());
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(kDigits[buf[i] >> 4]);
        out.push_back(kDigits[buf[i] & 0x0F]);
    }
    return out;
}

// What the closest-to-EOF candidate looked like, kept purely so a failure on
// a real device reports something actionable instead of just "corrupted".
// Listing is the one step that cannot be reproduced off-device without the
// user's own multi-gigabyte .bin files.
struct CandidateDiagnostics {
    bool attempted = false;
    uint64_t descrPos = 0;
    uint64_t descrSize = 0;
    uint32_t expectedCrc = 0;
    uint32_t actualCrc = 0;
    std::string tailHex;
};

// Reads the little-endian uint32 at buf[offset..offset+4).
uint32_t readLe32(const std::vector<uint8_t>& buf, size_t offset) {
    return static_cast<uint32_t>(buf[offset]) | (static_cast<uint32_t>(buf[offset + 1]) << 8) |
        (static_cast<uint32_t>(buf[offset + 2]) << 16) | (static_cast<uint32_t>(buf[offset + 3]) << 24);
}

// Length of the descriptor body (signature included, trailing CRC excluded),
// found by locating the split where the body's own CRC-32 matches the 4 bytes
// immediately after it.
//
// The reference (MEMORY_BUFFER::openWithCRCAtEnd) hardcodes this split at
// `window - 4`, which holds because the footer descriptor is normally the very
// last thing in the archive. That is tried first here, so a well-formed
// archive takes the exact reference path. Real repack .bin sets were observed
// to carry a few bytes past the descriptor, though, which makes the fixed
// split overshoot and CRC-fail a perfectly good descriptor; scanning for the
// split recovers those. A CRC-32 agreeing by chance is a 1-in-2^32 event, so
// this cannot silently accept a wrong split.
std::optional<size_t> findCrcValidatedBodyLength(const std::vector<uint8_t>& buf) {
    // 9 = signature(4) + at least one byte per remaining field, before the CRC.
    constexpr size_t kMinBodyLen = 9;
    if (buf.size() < kMinBodyLen + 4) {
        return std::nullopt;
    }
    const size_t maxBodyLen = buf.size() - 4;

    if (crc32(buf.data(), maxBodyLen) == readLe32(buf, maxBodyLen)) {
        return maxBodyLen; // the reference split — the overwhelmingly common case
    }

    // Otherwise walk the remaining splits, carrying the CRC forward one byte at
    // a time rather than recomputing it for every candidate length.
    uint32_t running = crc32(buf.data(), kMinBodyLen);
    for (size_t bodyLen = kMinBodyLen; bodyLen < maxBodyLen; ++bodyLen) {
        if (running == readLe32(buf, bodyLen)) {
            return bodyLen;
        }
        running = crc32(buf.data() + bodyLen, 1, running);
    }
    return std::nullopt;
}

// Parses a footer local descriptor at `descrPos`, mirroring
// LOCAL_BLOCK_DESCRIPTOR + MEMORY_BUFFER::openWithCRCAtEnd from the real
// Unarc source (native/third_party/freearc/Unarc/ArcStructure.h), which this
// repository vendors:
//
//   descr_size = min(filesize - descr_pos, MAX_FOOTER_DESCRIPTOR_SIZE)
//   right_crc  = last 4 bytes of that window
//   crc        = CalcCRC(window, descr_size - 4)      // CRC-32, zlib-compatible
//
// i.e. the descriptor's body runs from its signature to 4 bytes before the end
// of the window, and is not delimited by parsing its fields. That split is
// tried first; see findCrcValidatedBodyLength for why a shorter one is also
// considered. Returns std::nullopt when no split CRC-validates — a
// coincidental byte match rather than the real descriptor. Once the CRC has
// validated, any further inconsistency is real corruption and is thrown,
// matching the reference's CHECK() calls.
std::optional<BlockDescriptor> tryParseFooterLocalDescriptor(
    const std::vector<uint8_t>& buf, uint64_t descrPos, CandidateDiagnostics* diagnostics) {
    if (buf.size() < 8) {
        return std::nullopt; // no room for a signature plus the trailing CRC
    }
    const std::optional<size_t> validated = findCrcValidatedBodyLength(buf);

    if (diagnostics != nullptr && !diagnostics->attempted) {
        const size_t referenceBodyLen = buf.size() - 4;
        diagnostics->attempted = true;
        diagnostics->descrPos = descrPos;
        diagnostics->descrSize = buf.size();
        diagnostics->expectedCrc = readLe32(buf, referenceBodyLen);
        diagnostics->actualCrc = crc32(buf.data(), referenceBodyLen);
        diagnostics->tailHex = toHex(buf, 64);
    }

    // Parse the fields forward from the signature. They are self-delimiting,
    // so this works whether or not the CRC agreed.
    BlockDescriptor descriptor;
    size_t parsedBodyLen;
    try {
        ByteReader reader(buf.data(), validated ? *validated : buf.size());
        reader.skip(4); // signature, already matched by the caller
        descriptor.type = static_cast<BlockType>(reader.readVleInt());
        descriptor.compressor = reader.readString();
        descriptor.origSize = reader.readVleInt();
        descriptor.compSize = reader.readVleInt();
        descriptor.crc = reader.readFixed4();
        parsedBodyLen = reader.position(); // skip(4) above already counted the signature
    } catch (const ArcFormatError&) {
        return std::nullopt; // not a descriptor at all
    }

    // Structural checks, same as the reference's CHECK() calls.
    const bool structurallyValid = descriptor.type == BlockType::Footer && descriptor.origSize > 0 &&
        descriptor.compSize > 0 && descriptor.compSize <= descrPos && !descriptor.compressor.empty() &&
        std::all_of(descriptor.compressor.begin(), descriptor.compressor.end(),
                    [](unsigned char c) { return c >= 0x20 && c < 0x7F; });

    if (validated) {
        if (descriptor.type != BlockType::Footer) {
            throw ArcFormatError("archive structure corrupted (footer block not found)");
        }
        if (!structurallyValid) {
            throw ArcFormatError("archive structure corrupted (strange descriptor)");
        }
        descriptor.pos = descrPos - descriptor.compSize;
        return descriptor;
    }

    // No split CRC-validated. Repacker-built archives exist whose descriptor
    // checksum matches no standard CRC-32 over its own bytes (verified against
    // real .bin files: same fields, same layout, stored value unreachable by
    // CRC-32/32C/BZIP2/POSIX/MPEG-2/Adler-32 over every range and split), so a
    // descriptor whose fields are all individually sound is accepted on their
    // strength alone rather than declaring a readable archive corrupt. The
    // checks above are what makes that safe, and nothing downstream is
    // weakened: every control block still has to decompress and pass its own
    // CRC before any entry is reported.
    if (!structurallyValid || parsedBodyLen + 4 > buf.size()) {
        return std::nullopt;
    }
    descriptor.pos = descrPos - descriptor.compSize;
    return descriptor;
}

} // namespace

ArcReader::ArcReader(const std::string& path) : file_(path) {}
ArcReader::ArcReader(const std::vector<std::string>& paths) : file_(paths) {}
ArcReader::ArcReader(int fd) : file_(fd) {}
ArcReader::ArcReader(const std::vector<int>& fds) : file_(fds) {}

BlockDescriptor ArcReader::findAndReadFooterLocalDescriptor() {
    const uint64_t fileSize = file_.size();
    const uint64_t windowSize = std::min<uint64_t>(fileSize, kMaxFooterDescriptorSize);
    if (windowSize < 4) {
        throw ArcFormatError("file too small to be a FreeArc archive");
    }
    const uint64_t windowStart = fileSize - windowSize;
    std::vector<uint8_t> window = file_.readAt(windowStart, windowSize);

    // Scan backward from EOF for the signature, like the reference
    // FindFooterDescriptor. The reference stops at the first (closest to EOF)
    // match and fails outright if its CRC doesn't validate; this keeps walking
    // toward the start of the window instead, which is a strict superset: a
    // valid archive resolves on that same first candidate, and an archive with
    // trailing bytes after its footer still resolves instead of being declared
    // corrupt (the 4-byte signature can also recur by coincidence — see
    // docs/ANALYSIS.md §1).
    CandidateDiagnostics diagnostics;
    size_t candidates = 0;
    for (size_t i = windowSize - 4; ; --i) {
        if (matchesSignatureAt(window, i)) {
            ++candidates;
            const uint64_t descrPos = windowStart + i;
            const uint64_t readSize = std::min<uint64_t>(fileSize - descrPos, kMaxFooterDescriptorSize);
            std::vector<uint8_t> buf = file_.readAt(descrPos, readSize);
            if (auto descriptor = tryParseFooterLocalDescriptor(buf, descrPos, &diagnostics)) {
                return *descriptor;
            }
        }
        if (i == 0) break;
    }

    // Everything below is a failure path. Report what was actually seen: this
    // is the only step that cannot be reproduced without the user's own files,
    // so a bare "corrupted" costs a full test round-trip on a real device.
    std::string partSizes;
    for (size_t i = 0; i < file_.partCount(); ++i) {
        partSizes += (i == 0 ? "" : "+") + std::to_string(file_.partSize(i));
    }
    std::string where = " [size=" + std::to_string(fileSize) + " parts=" + partSizes +
        " candidates=" + std::to_string(candidates);
    if (diagnostics.attempted) {
        char crcBuf[64];
        std::snprintf(crcBuf, sizeof(crcBuf), " crc=%08x want=%08x", diagnostics.actualCrc, diagnostics.expectedCrc);
        where += " pos=" + std::to_string(diagnostics.descrPos) + " len=" + std::to_string(diagnostics.descrSize) +
            std::string(crcBuf) + " bytes=" + diagnostics.tailHex;
    }
    where += "]";

    if (candidates == 0) {
        throw ArcFormatError("this is not a FreeArc archive, or it is corrupt (no footer signature found)" + where);
    }
    throw ArcFormatError("archive structure corrupted (footer descriptor failed CRC check)" + where);
}

std::vector<uint8_t> ArcReader::decompressBlock(const BlockDescriptor& block) {
    std::vector<uint8_t> raw = file_.readAt(block.pos, block.compSize);

    std::vector<uint8_t> decompressed;
    if (block.compressor.empty() || block.compressor == "store") {
        if (block.compSize != block.origSize) {
            throw ArcFormatError("archive structure corrupted (store size mismatch)");
        }
        decompressed = std::move(raw);
    } else if (block.compressor.compare(0, 5, "lzma:") == 0 || block.compressor == "lzma") {
        if (!isLzmaSupported()) {
            throw UnsupportedCompressorError(block.compressor);
        }
        if (!lzmaDecode(block.compressor, raw, block.origSize, decompressed)) {
            throw ArcFormatError("archive structure corrupted (LZMA block failed to decode)");
        }
    } else {
        throw UnsupportedCompressorError(block.compressor);
    }

    const uint32_t actualCrc = crc32(decompressed.data(), decompressed.size());
    if (actualCrc != block.crc && !ignoreBlockCrc_) {
        throw BlockCrcMismatchError("archive structure corrupted (block failed CRC check)");
    }
    return decompressed;
}

std::vector<BlockDescriptor> ArcReader::readFooterControlBlocks(const BlockDescriptor& footerLocalDescriptor) {
    std::vector<uint8_t> body = decompressBlock(footerLocalDescriptor);
    ByteReader reader(body.data(), body.size());

    const uint64_t count = reader.readVleInt();
    std::vector<BlockDescriptor> blocks;
    blocks.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
        BlockDescriptor b;
        b.type = static_cast<BlockType>(reader.readVleInt());
        b.compressor = reader.readString();
        b.pos = reader.readVleInt(); // relative to footer content start, resolved below.
        b.origSize = reader.readVleInt();
        b.compSize = reader.readVleInt();
        b.crc = reader.readFixed4();
        blocks.push_back(std::move(b));
    }
    for (auto& b : blocks) {
        b.pos = footerLocalDescriptor.pos - b.pos;
    }
    return blocks;
}

void ArcReader::readDirectoryBlockInto(const BlockDescriptor& dirBlockDescriptor, std::vector<ArcEntry>& out) {
    std::vector<uint8_t> body = decompressBlock(dirBlockDescriptor);
    ByteReader r(body.data(), body.size());

    const uint64_t numOfBlocks = r.readVleInt();
    std::vector<uint64_t> numOfFiles(numOfBlocks);
    for (auto& v : numOfFiles) v = r.readVleInt();

    std::vector<std::string> compressors(numOfBlocks);
    for (auto& v : compressors) v = r.readString();

    std::vector<uint64_t> offsets(numOfBlocks);
    for (auto& v : offsets) v = r.readVleInt();

    std::vector<uint64_t> compSizes(numOfBlocks);
    for (auto& v : compSizes) v = r.readVleInt();
    (void)compSizes; // kept for parity with the source layout (must still be consumed from the stream).

    // Absolute file offset of each solid/data block's compressed bytes:
    // data_block[i].pos = block_info.pos - offsets[i], i.e. each data block
    // sits `offsets[i]` bytes before this directory block's own content.
    std::vector<uint64_t> dataBlockPos(numOfBlocks);
    for (size_t i = 0; i < numOfBlocks; ++i) {
        if (offsets[i] > dirBlockDescriptor.pos) {
            throw ArcFormatError("archive structure corrupted (data block offset out of range)");
        }
        dataBlockPos[i] = dirBlockDescriptor.pos - offsets[i];
    }

    // Cumulative running total, exactly as DIRECTORY_BLOCK's constructor
    // does: after this loop, numOfFiles[i] is the file-index upper bound
    // (exclusive) for solid/data block i.
    uint64_t totalFiles = 0;
    for (auto& v : numOfFiles) {
        totalFiles += v;
        v = totalFiles;
    }

    const uint64_t dirsCount = r.readVleInt();
    std::vector<std::string> dirs(dirsCount);
    for (auto& v : dirs) v = r.readString();

    std::vector<std::string> names(totalFiles);
    for (auto& v : names) v = r.readString();

    std::vector<uint64_t> dirNumbers(totalFiles);
    for (auto& v : dirNumbers) v = r.readVleInt();

    std::vector<uint64_t> sizes(totalFiles);
    for (auto& v : sizes) v = r.readVleInt();

    for (uint64_t i = 0; i < totalFiles; ++i) r.readFixed4(); // time[], unused for listing.

    std::vector<uint8_t> isDir(totalFiles);
    for (auto& v : isDir) v = r.readFixed1();

    std::vector<uint32_t> crcs(totalFiles);
    for (auto& v : crcs) v = r.readFixed4();

    for (uint64_t i = 0; i < totalFiles; ++i) {
        size_t blockIndex = 0;
        while (blockIndex < numOfFiles.size() && i >= numOfFiles[blockIndex]) ++blockIndex;
        if (blockIndex >= numOfFiles.size()) {
            throw ArcFormatError("archive structure corrupted (file not mapped to any data block)");
        }

        if (dirNumbers[i] >= dirs.size()) {
            throw ArcFormatError("archive structure corrupted (directory index out of range)");
        }
        const std::string& dirName = dirs[dirNumbers[i]];
        std::string path = dirName.empty() ? names[i] : dirName + "/" + names[i];

        ArcEntry entry;
        entry.path = std::move(path);
        entry.kind = isDir[i] ? EntryKind::Directory : EntryKind::File;
        entry.uncompressedSize = sizes[i];
        entry.crc = crcs[i];
        entry.dataBlockIndex = blockIndex;
        entry.dataBlockCompressor = compressors[blockIndex];
        entry.dataBlockAbsolutePos = dataBlockPos[blockIndex];
        out.push_back(std::move(entry));
    }
}

std::vector<ArcEntry> ArcReader::list() {
    try {
        return listWithCurrentCrcPolicy();
    } catch (const BlockCrcMismatchError&) {
        // Strict CRC checking is the default and just failed. Before calling
        // the archive corrupt, retry without it: repacker-built archives store
        // checksums that disagree with standard CRC-32 while decoding
        // perfectly well — established on this archive's own footer
        // descriptor, whose stored value is unreachable by CRC-32 under any
        // range, split, polynomial, init or bit order.
        //
        // This does not accept whatever the decoder produced. A control block
        // is a chain of VLE integers, NUL-terminated strings and
        // bounds-checked counts, so a genuinely wrong decode fails the parse
        // below and that error propagates instead.
        ignoreBlockCrc_ = true;
        return listWithCurrentCrcPolicy();
    }
}

std::vector<ArcEntry> ArcReader::listWithCurrentCrcPolicy() {
    BlockDescriptor footerLocal = findAndReadFooterLocalDescriptor();
    std::vector<BlockDescriptor> controlBlocks = readFooterControlBlocks(footerLocal);

    std::vector<ArcEntry> entries;
    for (const auto& block : controlBlocks) {
        if (block.type == BlockType::Directory) {
            readDirectoryBlockInto(block, entries);
        }
    }
    return entries;
}

namespace {

template <typename Part>
std::vector<ArcEntry> listPartsImpl(const std::vector<Part>& parts) {
    if (parts.empty()) {
        throw ArcFormatError("no archive parts given");
    }

    if (parts.size() == 1) {
        return ArcReader(parts).list();
    }

    // Layout 1: one archive split across volumes — only the concatenation has
    // a footer at its end.
    std::vector<ArcEntry> asWhole;
    std::string wholeFailure;
    try {
        asWhole = ArcReader(parts).list();
    } catch (const UnsupportedCompressorError&) {
        throw; // a real codec limit, not a wrong guess about the layout
    } catch (const std::runtime_error& e) {
        // ArcFormatError, or an out-of-range read from following a position
        // that only makes sense under the other layout.
        wholeFailure = e.what();
    }

    // Layout 2: each part is a self-contained archive. Positions are rebased
    // onto the concatenated stream so extraction stays uniform.
    std::vector<ArcEntry> asParts;
    std::string partFailures;
    uint64_t base = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        const uint64_t partSize = FileSource(parts[i]).size();
        try {
            for (ArcEntry entry : ArcReader(parts[i]).list()) {
                entry.dataBlockAbsolutePos += base;
                asParts.push_back(std::move(entry));
            }
        } catch (const UnsupportedCompressorError&) {
            throw;
        } catch (const std::runtime_error& e) {
            partFailures += " part" + std::to_string(i + 1) + "=" + e.what();
        }
        base += partSize;
    }

    // Both interpretations can "work" while one of them is wrong: reading a
    // set of side-by-side archives as one concatenation finds only the last
    // one's footer and silently lists just that archive. Whichever sees more
    // of the data is the one that matched the real layout.
    if (asParts.size() > asWhole.size()) {
        return asParts;
    }
    if (!asWhole.empty()) {
        return asWhole;
    }
    throw ArcFormatError(wholeFailure + partFailures);
}

} // namespace

std::vector<ArcEntry> listArchiveParts(const std::vector<std::string>& paths) {
    return listPartsImpl(paths);
}

std::vector<ArcEntry> listArchiveParts(const std::vector<int>& fds) {
    return listPartsImpl(fds);
}

} // namespace arcextract
