// Zero-dependency test harness (no GoogleTest etc. required) so this
// compiles the same way with a plain host g++ (used to verify this code
// in an environment without the Android NDK — see docs/ANALYSIS.md §7) and
// later with CTest once wired into native/CMakeLists.txt.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../arc/arc_reader.h"
#include "../arc/lzma_decoder.h"
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

void testReadsDescriptorFollowedByTrailingBytes() {
    // Observed on a real 20 GB repack: the footer descriptor is not the very
    // last thing in the file, so the reference's fixed "body = window - 4"
    // split overshoots and CRC-fails a perfectly valid descriptor. The reader
    // locates the split by CRC instead and recovers.
    ArcReader reader(fixturePath("sample_trailing_bytes.arc"));
    std::vector<ArcEntry> entries = reader.list();
    expectTrue(entries.size() == 2, "an archive with bytes after its footer descriptor still lists its 2 files");
}

void testReadsDescriptorWhoseCrcDoesNotValidate() {
    // Real repacker-built .bin files (verified against a 20 GB repack) carry a
    // footer descriptor whose fields are all sound — signature, type FOOTER,
    // a printable compressor string, plausible sizes — but whose trailing
    // checksum matches no standard CRC-32 over its own bytes. Reading it must
    // not hinge on that checksum.
    ArcReader reader(fixturePath("sample_bad_descriptor_crc.arc"));
    std::vector<ArcEntry> entries = reader.list();
    expectTrue(entries.size() == 2, "an archive whose descriptor CRC does not validate still lists its files");
}

void testListsArchiveWithLzmaControlBlocks() {
    // Every real repack compresses its control blocks with LZMA
    // ("lzma:mfbt4:d1m" on the archive this was built against), so listing one
    // is impossible without a working decoder. FreeArc stores no properties
    // header, so this also pins that lc/lp/pb and the dictionary size are
    // recovered from the method string.
    ArcReader reader(fixturePath("sample_lzma.arc"));
    std::vector<ArcEntry> entries = reader.list();
    expectTrue(entries.size() == 2, "an archive whose control blocks are LZMA-compressed lists its 2 files");

    bool foundHello = false;
    for (const auto& e : entries) {
        if (e.path == "data/hello.txt") {
            foundHello = true;
            expectTrue(e.uncompressedSize == 47, "the LZMA-decoded directory reports the right size");
        }
    }
    expectTrue(foundHello, "entry names survive the LZMA-decoded directory block");
}

void testParsesLzmaMethodString() {
    LzmaParams params = parseLzmaMethod("lzma:mfbt4:d1m");
    expectTrue(params.dictSize == 1u << 20, "d1m is read as a 1 MB dictionary");
    expectTrue(params.litContextBits == 3 && params.litPosBits == 0 && params.posStateBits == 2,
               "lc/lp/pb fall back to LZMA_METHOD's defaults");

    LzmaParams explicitParams = parseLzmaMethod("lzma:d64m:lc1:lp2:pb1");
    expectTrue(explicitParams.dictSize == 64u << 20 && explicitParams.litContextBits == 1 &&
                   explicitParams.litPosBits == 2 && explicitParams.posStateBits == 1,
               "explicit lc/lp/pb/dictionary parameters override the defaults");
}

void testReportsDiagnosticsOnFooterFailure() {
    // A footer failure can only happen against files that cannot be
    // reproduced off-device, so the message must carry enough to diagnose it
    // from a single screenshot.
    bool threw = false;
    std::string message;
    try {
        ArcReader reader(fixturePath("not_an_arc_but_has_signature.bin"));
        reader.list();
    } catch (const ArcFormatError& e) {
        threw = true;
        message = e.what();
    }
    expectTrue(threw, "a file whose only ArC signature is coincidental is rejected");
    expectTrue(message.find("size=") != std::string::npos && message.find("parts=") != std::string::npos &&
                   message.find("crc=") != std::string::npos && message.find("bytes=") != std::string::npos,
               "the failure message carries size/parts/crc/bytes diagnostics");
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

void testListsSideBySideIndependentArchives() {
    // A repack's .bin files are not always volumes of one archive: the part
    // sizes seen on a real 20 GB repack (20.18 GB + 150 MB + 16 MB) are the
    // side-by-side layout instead. Each part must be read as its own archive,
    // with positions rebased onto the concatenated stream.
    std::vector<std::string> parts = {
        fixturePath("sample_independent-01.bin"),
        fixturePath("sample_independent-02.bin"),
    };
    std::vector<ArcEntry> entries = listArchiveParts(parts);
    expectTrue(entries.size() == 3, "both independent archives are listed together (2 + 1 entries)");

    const ArcEntry* extra = nullptr;
    for (const auto& e : entries) {
        if (e.path == "bonus/extra.txt") extra = &e;
    }
    expectTrue(extra != nullptr, "an entry from the second archive is present");
    if (!extra) return;

    // Rebased onto the concatenated stream, so one fd set serves extraction.
    FileSource whole(parts);
    std::vector<uint8_t> content = whole.readAt(extra->dataBlockAbsolutePos, extra->uncompressedSize);
    std::string text(content.begin(), content.end());
    expectTrue(text == "A second, independent archive.\n",
               "the second archive's content reads back at its rebased position");
}

void testMultiPartStillPrefersTheConcatenation() {
    // The split-volume layout must keep working: listArchiveParts tries the
    // concatenation first, so this resolves there rather than per part.
    std::vector<std::string> parts = {
        fixturePath("sample_multipart-01.bin"),
        fixturePath("sample_multipart-02.bin"),
        fixturePath("sample_multipart-03.bin"),
    };
    expectTrue(listArchiveParts(parts).size() == 2, "a split-volume set still lists via the concatenation");
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
    testReadsDescriptorFollowedByTrailingBytes();
    testReadsDescriptorWhoseCrcDoesNotValidate();
    testParsesLzmaMethodString();
    testListsArchiveWithLzmaControlBlocks();
    testReportsDiagnosticsOnFooterFailure();
    testListsAndExtractsAcrossMultiPartBoundary();
    testListsSideBySideIndependentArchives();
    testMultiPartStillPrefersTheConcatenation();
    testRejectsNonArchiveFile();

    if (failures > 0) {
        std::fprintf(stderr, "\n%d test(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nAll tests passed.\n");
    return 0;
}
