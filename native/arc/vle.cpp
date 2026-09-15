#include "vle.h"

namespace arcextract {

VleResult readVle(const uint8_t* data, size_t size, size_t offset) {
    VleResult result;
    if (offset >= size) return result;

    const uint8_t b0 = data[offset];

    if (b0 == 0xFF) {
        // 9-byte escape form: skip the 0xFF marker, then 8 raw
        // little-endian value bytes.
        if (offset + 9 > size) return result;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(data[offset + 1 + i]) << (8 * i);
        }
        result.value = v;
        result.bytesConsumed = 9;
        result.ok = true;
        return result;
    }

    // Count trailing 1-bits in b0 (0..7) to get the byte count n (1..8).
    size_t n = 1;
    while (n <= 7 && ((b0 >> (n - 1)) & 1) == 1) {
        ++n;
    }
    // n is now 1..8: number of bytes the field occupies.
    if (offset + n > size) return result;

    uint64_t raw = 0;
    for (size_t i = 0; i < n; ++i) {
        raw |= static_cast<uint64_t>(data[offset + i]) << (8 * i);
    }
    result.value = raw >> n;
    result.bytesConsumed = n;
    result.ok = true;
    return result;
}

void writeVle(uint64_t value, std::vector<uint8_t>& out) {
    // Smallest n in 1..8 such that value fits in 7*n bits.
    size_t n = 1;
    while (n < 8 && value >= (uint64_t(1) << (7 * n))) {
        ++n;
    }
    if (n == 8 && value >= (uint64_t(1) << 56)) {
        // Does not fit even the 56-bit (n=8) form -> use the 9-byte escape.
        out.push_back(0xFF);
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
        }
        return;
    }
    const uint64_t flag = (n == 8) ? 0x7Full : ((uint64_t(1) << (n - 1)) - 1);
    const uint64_t raw = (value << n) | flag;
    for (size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<uint8_t>((raw >> (8 * i)) & 0xFF));
    }
}

} // namespace arcextract
