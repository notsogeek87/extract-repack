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
class FileSource {
public:
    explicit FileSource(const std::string& path);

    // Wraps an already-open POSIX file descriptor (e.g. one obtained from
    // Android's ContentResolver.openFileDescriptor() for a SAF Uri that
    // has no plain filesystem path). Duplicates the fd so this object's
    // lifetime is independent of the caller's — closing the original fd
    // elsewhere does not invalidate reads made through this FileSource,
    // and destroying this FileSource never closes the caller's original
    // fd, only the dup'd copy.
    explicit FileSource(int fd);

    ~FileSource();

    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    uint64_t size() const { return size_; }

    // Reads exactly `length` bytes starting at `offset`. Throws
    // std::runtime_error if that range is out of bounds or a short read
    // occurs (treated as a corrupted/truncated archive by callers).
    std::vector<uint8_t> readAt(uint64_t offset, uint64_t length) const;

private:
    FILE* file_;
    uint64_t size_;
};

} // namespace arcextract
