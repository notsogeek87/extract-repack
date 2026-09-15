#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace arcextract {

// Mirrors the BLOCKTYPE enum in Unarc/ArcStructure.h exactly (values
// matter: they are read directly off disk, not just internal labels).
enum class BlockType : int {
    Descriptor = 0,
    Header = 1,
    Data = 2,
    Directory = 3,
    Footer = 4,
    Recovery = 5,
};

// Signature bytes "ArC" + 0x01, i.e. aSIGNATURE = make4byte(65,114,67,1)
// from the original source. Compared byte-for-byte, so this works
// identically on any little-endian host including ARM64 Android.
constexpr uint8_t kSignature[4] = {0x41, 0x72, 0x43, 0x01};
constexpr size_t kMaxFooterDescriptorSize = 4096; // MAX_FOOTER_DESCRIPTOR_SIZE

// A control (or data) block's descriptor as recorded either in the
// FOOTER's own list (position relative to the footer) or as a local
// descriptor immediately following a block's content.
struct BlockDescriptor {
    BlockType type = BlockType::Descriptor;
    std::string compressor; // e.g. "store"; anything else needs a codec backend we may not have.
    uint64_t pos = 0;       // absolute offset in the file, once resolved.
    uint64_t origSize = 0;
    uint64_t compSize = 0;
    uint32_t crc = 0;
};

enum class EntryKind { File, Directory };

// One file/directory entry discovered while listing an archive — this is
// the native-side counterpart of the Kotlin `core` model's ArchiveEntry;
// the JNI layer (native/jni/, see docs/ANALYSIS.md §6) maps one to the
// other field-for-field.
struct ArcEntry {
    std::string path; // already joined "dir/name", forward slashes, NOT yet security-validated.
    EntryKind kind = EntryKind::File;
    uint64_t uncompressedSize = 0;
    uint32_t crc = 0;
    // Which solid/data block this file lives in, and its 0-based index
    // within that block's file range — needed to actually extract it later
    // without decompressing every other file sharing the same solid block
    // first is not always possible (solid compression is sequential), but
    // this at least tells the extractor which block to decompress.
    size_t dataBlockIndex = 0;
    std::string dataBlockCompressor;
    uint64_t dataBlockAbsolutePos = 0; // absolute file offset where that data/solid block's compressed bytes start.
};

} // namespace arcextract
