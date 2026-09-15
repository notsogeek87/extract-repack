#include "file_source.h"

#include <algorithm>
#include <stdexcept>
#include <unistd.h>

namespace arcextract {

namespace {

uint64_t determineSizeOrThrow(FILE* file, const std::string& label) {
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        throw std::runtime_error("cannot seek file: " + label);
    }
    long end = std::ftell(file);
    if (end < 0) {
        std::fclose(file);
        throw std::runtime_error("cannot determine size of file: " + label);
    }
    return static_cast<uint64_t>(end);
}

} // namespace

void FileSource::addPathPart(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        throw std::runtime_error("cannot open file: " + path);
    }
    uint64_t size = determineSizeOrThrow(file, path);
    parts_.push_back(Part{file, size, totalSize_});
    totalSize_ += size;
}

void FileSource::addFdPart(int fd) {
    int duped = ::dup(fd);
    if (duped < 0) {
        throw std::runtime_error("cannot dup file descriptor");
    }
    FILE* file = ::fdopen(duped, "rb");
    if (!file) {
        ::close(duped);
        throw std::runtime_error("cannot fdopen file descriptor");
    }
    uint64_t size = determineSizeOrThrow(file, "fd");
    parts_.push_back(Part{file, size, totalSize_});
    totalSize_ += size;
}

FileSource::FileSource(const std::string& path) {
    addPathPart(path);
}

FileSource::FileSource(const std::vector<std::string>& paths) {
    if (paths.empty()) {
        throw std::runtime_error("no file parts given");
    }
    for (const std::string& path : paths) {
        addPathPart(path);
    }
}

FileSource::FileSource(int fd) {
    addFdPart(fd);
}

FileSource::FileSource(const std::vector<int>& fds) {
    if (fds.empty()) {
        throw std::runtime_error("no file parts given");
    }
    for (int fd : fds) {
        addFdPart(fd);
    }
}

FileSource::~FileSource() {
    for (const Part& part : parts_) {
        std::fclose(part.file);
    }
}

std::vector<uint8_t> FileSource::readAt(uint64_t offset, uint64_t length) const {
    if (offset > totalSize_ || length > totalSize_ - offset) {
        throw std::runtime_error("read out of file bounds");
    }
    std::vector<uint8_t> result(length);
    uint64_t remaining = length;
    uint64_t cursor = offset;
    size_t destPos = 0;

    for (const Part& part : parts_) {
        if (remaining == 0) break;
        const uint64_t partEnd = part.cumulativeStart + part.size;
        if (cursor >= partEnd) continue; // logical range starts at or after this part
        const uint64_t localOffset = cursor - part.cumulativeStart;
        const uint64_t availableInPart = part.size - localOffset;
        const uint64_t toRead = std::min<uint64_t>(availableInPart, remaining);

        if (std::fseek(part.file, static_cast<long>(localOffset), SEEK_SET) != 0) {
            throw std::runtime_error("seek failed");
        }
        if (toRead > 0) {
            size_t got = std::fread(result.data() + destPos, 1, toRead, part.file);
            if (got != toRead) {
                throw std::runtime_error("short read (truncated archive?)");
            }
        }
        destPos += toRead;
        cursor += toRead;
        remaining -= toRead;
    }

    if (remaining != 0) {
        // Bounds were checked against totalSize_ above, so this can only
        // happen if the parts don't actually cover [offset, offset+length) —
        // i.e. a logic bug here, not a caller error.
        throw std::runtime_error("internal error: file parts did not cover the requested range");
    }
    return result;
}

} // namespace arcextract
