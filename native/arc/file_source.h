#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace arcextract {

// Bounded, seekable read access to the underlying archive file. This is
// the only place that touches the filesystem, which keeps the format
// parsing logic (arc_reader.*) testable on any platform and easy to swap
// for a SAF-backed `ParcelFileDescriptor` source on Android without
// touching the parser itself — see docs/ANALYSIS.md §6.
//
// Deliberately never reads the whole file: every call takes an explicit
// offset and length, so a 19 GB .bin behaves the same as a 190-byte test
// fixture.
//
// Inno Setup installers split their external data into several sibling
// `.bin` files (`fg-01.bin`, `fg-02.bin`, ...) that Inno itself reads back
// as one continuous stream — the single FreeArc "ArC" container embedded
// inside is laid out across all of them, so a solid block, or the FOOTER
// itself, can straddle a `.bin` boundary. A `FileSource` constructed from
// several parts (in order) presents them as exactly that one logical
// stream: `size()` is the sum of every part's size, and `readAt()`
// transparently reads across part boundaries. A single-path/single-fd
// `FileSource` is just the one-part case of the same machinery.
class FileSource {
public:
    explicit FileSource(const std::string& path);
    explicit FileSource(const std::vector<std::string>& paths);

    // Wraps an already-open POSIX file descriptor (e.g. one obtained from
    // Android's ContentResolver.openFileDescriptor() for a SAF Uri that
    // has no plain filesystem path). Duplicates the fd so this object's
    // lifetime is independent of the caller's — closing the original fd
    // elsewhere does not invalidate reads made through this FileSource,
    // and destroying this FileSource never closes the caller's original
    // fd, only the dup'd copy.
    explicit FileSource(int fd);

    // Same idea, for the ordered set of `.bin` part fds making up one
    // logical archive.
    explicit FileSource(const std::vector<int>& fds);

    ~FileSource();

    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    uint64_t size() const { return totalSize_; }

    // Reads exactly `length` bytes starting at `offset` in the logical
    // (concatenated) stream, transparently spanning across part
    // boundaries as needed. Throws std::runtime_error if that range is
    // out of bounds or a short read occurs (treated as a
    // corrupted/truncated archive by callers).
    std::vector<uint8_t> readAt(uint64_t offset, uint64_t length) const;

private:
    struct Part {
        FILE* file;
        uint64_t size;
        uint64_t cumulativeStart;
    };

    void addPathPart(const std::string& path);
    void addFdPart(int fd);

    std::vector<Part> parts_;
    uint64_t totalSize_ = 0;
};

} // namespace arcextract
