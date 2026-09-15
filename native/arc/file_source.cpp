#include "file_source.h"

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

FileSource::FileSource(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) {
        throw std::runtime_error("cannot open file: " + path);
    }
    size_ = determineSizeOrThrow(file_, path);
}

FileSource::FileSource(int fd) {
    int duped = ::dup(fd);
    if (duped < 0) {
        throw std::runtime_error("cannot dup file descriptor");
    }
    file_ = ::fdopen(duped, "rb");
    if (!file_) {
        ::close(duped);
        throw std::runtime_error("cannot fdopen file descriptor");
    }
    size_ = determineSizeOrThrow(file_, "fd");
}

FileSource::~FileSource() {
    if (file_) std::fclose(file_);
}

std::vector<uint8_t> FileSource::readAt(uint64_t offset, uint64_t length) const {
    if (offset > size_ || length > size_ - offset) {
        throw std::runtime_error("read out of file bounds");
    }
    if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) {
        throw std::runtime_error("seek failed");
    }
    std::vector<uint8_t> buf(length);
    if (length > 0) {
        size_t got = std::fread(buf.data(), 1, length, file_);
        if (got != length) {
            throw std::runtime_error("short read (truncated archive?)");
        }
    }
    return buf;
}

} // namespace arcextract
