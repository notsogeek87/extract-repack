#include "arc_reader.h"

#include <algorithm>

#include "byte_reader.h"
#include "crc32.h"

namespace arcextract {

namespace {

bool matchesSignatureAt(const std::vector<uint8_t>& buf, size_t i) {
    return buf[i] == kSignature[0] && buf[i + 1] == kSignature[1] &&
           buf[i + 2] == kSignature[2] && buf[i + 3] == kSignature[3];
}

// Tries to parse a footer local descriptor out of `buf`, which starts at the
// candidate's signature bytes and extends up to `kMaxFooterDescriptorSize`
// bytes or EOF, whichever is smaller — a generous bound, not an assumption
// that the descriptor ends exactly at EOF (a RECOVERY block or other bytes
// may follow it; see docs/ANALYSIS.md §1/§3.1). The descriptor's own length
// is instead determined by parsing its self-delimiting fields forward from
// the signature. Returns std::nullopt for a candidate that is structurally
// malformed or whose CRC doesn't validate — i.e. a coincidental byte match,
// not the real footer descriptor. Once the CRC *has* validated, any further
// inconsistency (wrong block type, implausible sizes) is real corruption and
// is thrown rather than swallowed.
std::optional<BlockDescriptor> tryParseFooterLocalDescriptor(const std::vector<uint8_t>& buf, uint64_t descrPos) {
    size_t bodyLen;
    uint64_t type;
    std::string compressor;
    uint64_t origSize;
    uint64_t compSize;
    uint32_t crc;
    try {
        ByteReader fieldReader(buf.data() + 4, buf.size() - 4);
        type = fieldReader.readVleInt();
        compressor = fieldReader.readString();
        origSize = fieldReader.readVleInt();
        compSize = fieldReader.readVleInt();
        crc = fieldReader.readFixed4();
        bodyLen = 4 + fieldReader.position();
        if (bodyLen + 4 > buf.size()) {
            return std::nullopt; // no room left for the trailing CRC field
        }
    } catch (const ArcFormatError&) {
        return std::nullopt;
    }

    const uint32_t trailingCrc = static_cast<uint32_t>(buf[bodyLen]) | (static_cast<uint32_t>(buf[bodyLen + 1]) << 8) |
        (static_cast<uint32_t>(buf[bodyLen + 2]) << 16) | (static_cast<uint32_t>(buf[bodyLen + 3]) << 24);
    const uint32_t actualCrc = crc32(buf.data(), bodyLen);
    if (actualCrc != trailingCrc) {
        return std::nullopt;
    }

    if (static_cast<BlockType>(type) != BlockType::Footer) {
        throw ArcFormatError("archive structure corrupted (footer block not found)");
    }
    if (origSize == 0 || compSize == 0 || compSize > descrPos) {
        throw ArcFormatError("archive structure corrupted (implausible footer descriptor sizes)");
    }

    BlockDescriptor descriptor;
    descriptor.type = BlockType::Footer;
    descriptor.compressor = std::move(compressor);
    descriptor.origSize = origSize;
    descriptor.compSize = compSize;
    descriptor.crc = crc;
    descriptor.pos = descrPos - compSize;
    return descriptor;
}

} // namespace

ArcReader::ArcReader(const std::string& path) : file_(path) {}
ArcReader::ArcReader(int fd) : file_(fd) {}

BlockDescriptor ArcReader::findAndReadFooterLocalDescriptor() {
    const uint64_t fileSize = file_.size();
    const uint64_t windowSize = std::min<uint64_t>(fileSize, kMaxFooterDescriptorSize);
    if (windowSize < 4) {
        throw ArcFormatError("file too small to be a FreeArc archive");
    }
    const uint64_t windowStart = fileSize - windowSize;
    std::vector<uint8_t> window = file_.readAt(windowStart, windowSize);

    // Scan backward from the end, exactly like the original
    // FindFooterDescriptor: a byte-coincidental match can occur anywhere in
    // the window (see docs/ANALYSIS.md §1), so a candidate is only accepted
    // once it parses cleanly and its CRC validates — never by merely being
    // the occurrence closest to EOF. On a failed candidate, keep walking
    // toward the start of the window instead of giving up immediately.
    bool foundSignature = false;
    for (size_t i = windowSize - 4; ; --i) {
        if (matchesSignatureAt(window, i)) {
            foundSignature = true;
            const uint64_t descrPos = windowStart + i;
            const uint64_t readSize = std::min<uint64_t>(fileSize - descrPos, kMaxFooterDescriptorSize);
            std::vector<uint8_t> buf = file_.readAt(descrPos, readSize);
            if (std::optional<BlockDescriptor> descriptor = tryParseFooterLocalDescriptor(buf, descrPos)) {
                return *descriptor;
            }
            // Not the real footer descriptor (malformed fields or CRC mismatch) —
            // fall through and keep scanning backward for another candidate.
        }
        if (i == 0) break;
    }
    if (!foundSignature) {
        throw ArcFormatError("this is not a FreeArc archive, or it is corrupt (no footer signature found)");
    }
    throw ArcFormatError("archive structure corrupted (footer descriptor failed CRC check)");
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
