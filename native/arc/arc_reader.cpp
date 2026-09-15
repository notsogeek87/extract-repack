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
    // FindFooterDescriptor: the occurrence closest to EOF is taken, without
    // assuming it is the only one in the window (see docs/ANALYSIS.md §1
    // on why a byte-coincidental match elsewhere is expected and handled
    // by the CRC check below, not by uniqueness).
    bool found = false;
    size_t matchIndex = 0;
    for (size_t i = windowSize - 4; ; --i) {
        if (matchesSignatureAt(window, i)) {
            matchIndex = i;
            found = true;
            break;
        }
        if (i == 0) break;
    }
    if (!found) {
        throw ArcFormatError("this is not a FreeArc archive, or it is corrupt (no footer signature found)");
    }

    const uint64_t descrPos = windowStart + matchIndex;
    const uint64_t descrWindowSize = fileSize - descrPos; // always <= kMaxFooterDescriptorSize by construction
    if (descrWindowSize < 4) {
        throw ArcFormatError("archive structure corrupted (truncated footer descriptor)");
    }
    std::vector<uint8_t> descrWindow = file_.readAt(descrPos, descrWindowSize);

    const size_t bodyLen = descrWindow.size() - 4;
    const uint32_t trailingCrc =
        static_cast<uint32_t>(descrWindow[bodyLen]) | (static_cast<uint32_t>(descrWindow[bodyLen + 1]) << 8) |
        (static_cast<uint32_t>(descrWindow[bodyLen + 2]) << 16) | (static_cast<uint32_t>(descrWindow[bodyLen + 3]) << 24);
    const uint32_t actualCrc = crc32(descrWindow.data(), bodyLen);
    if (actualCrc != trailingCrc) {
        throw ArcFormatError("archive structure corrupted (footer descriptor failed CRC check)");
    }

    ByteReader reader(descrWindow.data(), bodyLen);
    const uint32_t sign = reader.readFixed4();
    if (sign != (static_cast<uint32_t>(kSignature[0]) | (static_cast<uint32_t>(kSignature[1]) << 8) |
                 (static_cast<uint32_t>(kSignature[2]) << 16) | (static_cast<uint32_t>(kSignature[3]) << 24))) {
        throw ArcFormatError("archive structure corrupted (bad footer descriptor signature)");
    }

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
        throw ArcFormatError("archive structure corrupted (implausible footer descriptor sizes)");
    }
    descriptor.pos = descrPos - descriptor.compSize;
    return descriptor;
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
