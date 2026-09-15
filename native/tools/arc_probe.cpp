// Small standalone CLI around ArcReader — the internal equivalent of
// `innoextract --list`, but for the ARC/FreeArc container layer, per
// docs/ANALYSIS.md §"ANALYSE AVANT EXTRACTION". Not shipped in the Android
// app; it exists so the exact same arc/ sources can be exercised from a
// desktop build (g++/cmake, no NDK required) during development, and as a
// quick manual diagnostic tool. The JNI bridge (native/jni/, see
// docs/ANALYSIS.md §6) calls the same ArcReader::list() this does.
#include <cstdio>
#include <string>

#include "../arc/arc_reader.h"

using namespace arcextract;

namespace {

const char* kindLabel(EntryKind k) { return k == EntryKind::Directory ? "DIR " : "FILE"; }

void printUsage(const char* argv0) {
    std::fprintf(stderr, "Usage: %s list <path-to.arc>\n", argv0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || std::string(argv[1]) != "list") {
        printUsage(argv[0]);
        return 2;
    }

    const std::string path = argv[2];
    try {
        ArcReader reader(path);
        std::vector<ArcEntry> entries = reader.list();
        std::printf("%-6s %12s  %-10s  %s\n", "TYPE", "SIZE", "METHOD", "PATH");
        for (const auto& e : entries) {
            std::printf(
                "%-6s %12llu  %-10s  %s\n",
                kindLabel(e.kind),
                static_cast<unsigned long long>(e.uncompressedSize),
                e.dataBlockCompressor.c_str(),
                e.path.c_str());
        }
        std::printf("\n%zu entries.\n", entries.size());
        return 0;
    } catch (const UnsupportedCompressorError& e) {
        std::fprintf(stderr, "Méthode de compression non supportée : %s\n", e.compressorId().c_str());
        return 3;
    } catch (const ArcFormatError& e) {
        std::fprintf(stderr, "Archive corrompue : %s\n", e.what());
        return 4;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Erreur : %s\n", e.what());
        return 1;
    }
}
