// Zero-dependency test harness (no GoogleTest etc. required) so this
// compiles the same way with a plain host g++ (used to verify this code
// in an environment without the Android NDK — see docs/ANALYSIS.md §7) and
// later with CTest once wired into native/CMakeLists.txt.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../arc/arc_reader.h"
#include "../arc/vle.h"

using namespace arcextract;

namespace {

int failures = 0;

void expectTrue(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    } else {
        std::printf("ok: %s\n", message);
    }
}

std::string fixturePath(const char* name) {
    // Tests are invoked with CWD = native/tests/ (see run_tests.sh / CTest WORKING_DIRECTORY).
    return std::string("fixtures/") + name;
}

void testVleRoundTrip() {
    uint64_t samples[] = {0, 1, 127, 128, 16383, 16384, 2097151, 2097152,
                           268435455, 268435456, 1ull << 40, 1ull << 55, ~0ull, 12345678901234ull};
    for (uint64_t v : samples) {
        std::vector<uint8_t> buf;
        writeVle(v, buf);
        VleResult r = readVle(buf.data(), buf.size(), 0);
        expectTrue(r.ok && r.value == v && r.bytesConsumed == buf.size(), "VLE round-trip");
    }
}

void testVleMatchesKnownRealBytes() {
    // Exact leading bytes of the real fg-01.bin reported by the user
    // (docs/ANALYSIS.md §1): 41 72 43 01 00 00 06 07 ...
    // Byte 4 (0x00) is a valid 1-byte VLE encoding of the value 0.
    uint8_t data[] = {0x41, 0x72, 0x43, 0x01, 0x00, 0x00, 0x06, 0x07};
    VleResult r = readVle(data, sizeof(data), 4);
    expectTrue(r.ok && r.bytesConsumed == 1 && r.value == 0, "VLE decodes byte 4 of the real header as a 1-byte zero");
}

void testListsStoreFixture() {
    ArcReader reader(fixturePath("sample_store.arc"));
    std::vector<ArcEntry> entries = reader.list();
    expectTrue(entries.size() == 2, "sample_store.arc lists exactly 2 files");

    bool foundHello = false, foundReadme = false;
    for (const auto& e : entries) {
        if (e.path == "data/hello.txt") {
            foundHello = true;
            expectTrue(e.kind == EntryKind::File, "hello.txt is a file");
            expectTrue(e.uncompressedSize == 47, "hello.txt has the expected size");
            expectTrue(e.dataBlockCompressor == "store", "hello.txt's data block is 'store'");
        }
        if (e.path == "readme.txt") {
            foundReadme = true;
            expectTrue(e.kind == EntryKind::File, "readme.txt is a file");
        }
    }
    expectTrue(foundHello, "found data/hello.txt in listing");
    expectTrue(foundReadme, "found readme.txt in listing (empty dir name -> no leading slash)");
}

void testExtractsStoredFileContent() {
    // Prove end-to-end: locate the file's data block via listing, then
    // read+verify its raw bytes directly through FileSource — this is the
    // "store" extraction path (native/arc/arc_reader.cpp only implements
    // structural listing; trivial store extraction composes directly from
    // FileSource + the entry's recorded data block position, which is
    // exactly what a future ExtractionEngine will do for non-store
    // codecs too once a real decoder is wired in).
    ArcReader reader(fixturePath("sample_store.arc"));
    std::vector<ArcEntry> entries = reader.list();

    const ArcEntry* helloEntry = nullptr;
    for (const auto& e : entries) {
        if (e.path == "data/hello.txt") helloEntry = &e;
    }
    expectTrue(helloEntry != nullptr, "located data/hello.txt for extraction");
    if (!helloEntry) return;

    FileSource file(fixturePath("sample_store.arc"));
    // The solid block holds both files concatenated; hello.txt is first,
    // so its content is the first `uncompressedSize` bytes of the block.
    std::vector<uint8_t> content = file.readAt(helloEntry->dataBlockAbsolutePos, helloEntry->uncompressedSize);
    std::string text(content.begin(), content.end());
    expectTrue(text == "Hello from inside a FreeArc-style solid block!\n", "extracted content matches exactly");
}

void testUnsupportedCodecIsReportedHonestly() {
    ArcReader reader(fixturePath("sample_unsupported_codec.arc"));
    bool threw = false;
    std::string compressorId;
    try {
        reader.list();
    } catch (const UnsupportedCompressorError& e) {
        threw = true;
        compressorId = e.compressorId();
    }
    expectTrue(threw, "listing an archive whose directory block uses lzma2 throws UnsupportedCompressorError");
    expectTrue(compressorId == "lzma2", "the unsupported compressor id is reported accurately");
}

void testCorruptionIsDetected() {
    ArcReader reader(fixturePath("sample_corrupt.arc"));
    bool threw = false;
    try {
        reader.list();
    } catch (const ArcFormatError&) {
        threw = true;
    }
    expectTrue(threw, "a flipped byte inside the directory block is caught by the CRC check (ArcFormatError)");
}

void testSkipsCoincidentalSignatureNearerEof() {
    // Real-world regression: a footer-signature-shaped byte sequence can sit
    // closer to EOF than the real footer without being one (see
    // docs/ANALYSIS.md §1). The reader must keep scanning backward past a
    // CRC-failing candidate instead of reporting the whole archive corrupt.
    ArcReader reader(fixturePath("sample_decoy_signature.arc"));
    std::vector<ArcEntry> entries = reader.list();
    expectTrue(entries.size() == 2, "sample_decoy_signature.arc still lists exactly 2 files past the decoy");
}

void testListsAndExtractsAcrossMultiPartBoundary() {
    // Real-world regression: a FitGirl-style repack splits one logical
    // FreeArc archive across sibling fg-01.bin/fg-02.bin/fg-03.bin files
    // (see docs/ANALYSIS.md §1) — the footer, and even a solid block's
    // content, can straddle a part boundary. ArcReader(paths) must treat
    // the ordered parts as one continuous stream, not just the first part.
    std::vector<std::string> parts = {
        fixturePath("sample_multipart-01.bin"),
        fixturePath("sample_multipart-02.bin"),
        fixturePath("sample_multipart-03.bin"),
    };
    ArcReader reader(parts);
    std::vector<ArcEntry> entries = reader.list();
    expectTrue(entries.size() == 2, "multi-part archive still lists exactly 2 files");

    const ArcEntry* helloEntry = nullptr;
    for (const auto& e : entries) {
        if (e.path == "data/hello.txt") helloEntry = &e;
    }
    expectTrue(helloEntry != nullptr, "located data/hello.txt across the multi-part archive");
    if (!helloEntry) return;

    FileSource multiPartFile(parts);
    std::vector<uint8_t> content = multiPartFile.readAt(helloEntry->dataBlockAbsolutePos, helloEntry->uncompressedSize);
    std::string text(content.begin(), content.end());
    expectTrue(text == "Hello from inside a FreeArc-style solid block!\n",
               "extracted content matches exactly when read across part boundaries");
}

void testRejectsNonArchiveFile() {
    // Not a FreeArc archive at all: no signature anywhere near EOF.
    bool threw = false;
    try {
        ArcReader reader(fixturePath("not_an_archive.bin"));
        reader.list();
    } catch (const ArcFormatError&) {
        threw = true;
    } catch (const std::runtime_error&) {
        threw = true; // e.g. file too small
    }
    expectTrue(threw, "a plain non-archive file is rejected rather than misparsed");
}

} // namespace

int main() {
    testVleRoundTrip();
    testVleMatchesKnownRealBytes();
    testListsStoreFixture();
    testExtractsStoredFileContent();
    testUnsupportedCodecIsReportedHonestly();
    testCorruptionIsDetected();
    testSkipsCoincidentalSignatureNearerEof();
    testListsAndExtractsAcrossMultiPartBoundary();
    testRejectsNonArchiveFile();

    if (failures > 0) {
        std::fprintf(stderr, "\n%d test(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nAll tests passed.\n");
    return 0;
}
