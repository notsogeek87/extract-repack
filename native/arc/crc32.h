#pragma once

#include <cstdint>
#include <cstddef>

namespace arcextract {

// Standard IEEE 802.3 / zlib / PKZIP CRC-32 (polynomial 0xEDB88320),
// which is what the FreeArc format doc specifies ("CRC algorithm used is
// pkzip's CRC-32"). Independent, public-domain-style implementation — this
// is a universally standard algorithm, not FreeArc-specific code.
uint32_t crc32(const uint8_t* data, size_t size, uint32_t seed = 0);

} // namespace arcextract
