#include "arc_reader.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "byte_reader.h"
#include "crc32.h"

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
    std::string headHex;
};

// Parses a footer local descriptor at `descrPos`, mirroring
// LOCAL_BLOCK_DESCRIPTOR + MEMORY_BUFFER::openWithCRCAtEnd from the real
// Unarc source (native/third_party/freearc/Unarc/ArcStructure.h), which this
// repository vendors:
//
//   descr_size = min(filesize - descr_pos, MAX_FOOTER_DESCRIPTOR_SIZE)
//   right_crc  = last 4 bytes of that window
//   crc        = CalcCRC(window, descr_size - 4)      // CRC-32, zlib-compatible
//
// i.e. the descriptor's body runs from its signature all the way to 4 bytes
// before the end of the window, and is *not* delimited by parsing its fields.
// Returns std::nullopt when the CRC doesn't validate — a coincidental byte
// match rather than the real descriptor. Once the CRC has validated, any
// further inconsistency is real corruption and is thrown, matching the
// reference's CHECK() calls.
std::optional<BlockDescriptor> tryParseFooterLocalDescriptor(
    const std::vector<uint8_t>& buf, uint64_t descrPos, CandidateDiagnostics* diagnostics) {
    if (buf.size() < 8) {
        return std::nullopt; // no room for a signature plus the trailing CRC
    }
    const size_t bodyLen = buf.size() - 4;
    const uint32_t expectedCrc = static_cast<uint32_t>(buf[bodyLen]) | (static_cast<uint32_t>(buf[bodyLen + 1]) << 8) |
        (static_cast<uint32_t>(buf[bodyLen + 2]) << 16) | (static_cast<uint32_t>(buf[bodyLen + 3]) << 24);
    const uint32_t actualCrc = crc32(buf.data(), bodyLen);

    if (diagnostics != nullptr && !diagnostics->attempted) {
        diagnostics->attempted = true;
        diagnostics->descrPos = descrPos;
        diagnostics->descrSize = buf.size();
        diagnostics->expectedCrc = expectedCrc;
        diagnostics->actualCrc = actualCrc;
        diagnostics->headHex = toHex(buf, 16);
    }

    if (actualCrc != expectedCrc) {
        return std::nullopt;
    }

    ByteReader reader(buf.data(), bodyLen);
    reader.skip(4); // signature, already matched by the caller
    BlockDescriptor descriptor;
    descriptor.type = static_cast<BlockType>(reader.readVleInt());
    descriptor.compressor = reader.readString();
    descriptor.origSize = reader.readVleInt();
    descriptor.compSize = reader.readVleInt();
    descriptor.crc = reader.readFixed4();

    if (descriptor.type != BlockType::Footer) {
        throw ArcFormatError("archive structure corrupted (footer block not found)");
    }
    if (descriptor.origSize == 0 || descriptor.compSize == 0 || descriptor.compSize > descrPos) {
        throw ArcFormatError("archive structure corrupted (strange descriptor)");
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
    std::string where = " [size=" + std::to_string(fileSize) + " parts=" + std::to_string(file_.partCount()) +
        " candidates=" + std::to_string(candidates);
    if (diagnostics.attempted) {
        char crcBuf[64];
        std::snprintf(crcBuf, sizeof(crcBuf), " crc=%08x want=%08x", diagnostics.actualCrc, diagnostics.expectedCrc);
        where += " pos=" + std::to_string(diagnostics.descrPos) + " len=" + std::to_string(diagnostics.descrSize) +
            std::string(crcBuf) + " head=" + diagnostics.headHex;
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
    } else {
        throw UnsupportedCompressorError(block.compressor);
    }

    const uint32_t actualCrc = crc32(decompressed.data(), decompressed.size());
    if (actualCrc != block.crc) {
        throw ArcFormatError("archive structure corrupted (block failed CRC check)");
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

} // namespace arcextract
