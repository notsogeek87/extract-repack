#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "vle.h"

namespace arcextract {

// Thrown for any structurally malformed input — truncated fields, a
// string missing its NUL terminator, a read past the end of the buffer.
// Callers surface this as "Archive corrompue" (see docs/ANALYSIS.md §5 /
// UserMessage.ArchiveCorrupted) rather than letting it propagate as a
// generic crash.
class ArcFormatError : public std::runtime_error {
public:
    explicit ArcFormatError(const std::string& message) : std::runtime_error(message) {}
};

// Sequential cursor over an already-decompressed control block, mirroring
// the read primitives of FreeArc's MEMORY_BUFFER (Unarc/ArcStructure.h)
// needed to parse FOOTER and DIRECTORY blocks: VLE integers, NUL-terminated
// strings, and fixed-width 1/4-byte fields.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool eof() const { return pos_ >= size_; }
    size_t remaining() const { return size_ > pos_ ? size_ - pos_ : 0; }

    uint64_t readVleInt() {
        VleResult r = readVle(data_, size_, pos_);
        if (!r.ok) {
            throw ArcFormatError("truncated variable-length integer");
        }
        pos_ += r.bytesConsumed;
        return r.value;
    }

    std::string readString() {
        size_t start = pos_;
        while (pos_ < size_ && data_[pos_] != 0) {
            ++pos_;
        }
        if (pos_ >= size_) {
            throw ArcFormatError("unterminated string field");
        }
        std::string s(reinterpret_cast<const char*>(data_ + start), pos_ - start);
        ++pos_; // consume the NUL
        return s;
    }

    uint32_t readFixed4() {
        requireBytes(4);
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data_[pos_ + i]) << (8 * i);
        pos_ += 4;
        return v;
    }

    uint8_t readFixed1() {
        requireBytes(1);
        return data_[pos_++];
    }

    void skip(size_t n) {
        requireBytes(n);
        pos_ += n;
    }

    size_t position() const { return pos_; }

private:
    void requireBytes(size_t n) const {
        if (pos_ + n > size_) {
            throw ArcFormatError("read past end of control block");
        }
    }

    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

} // namespace arcextract
