package eu.lielu.arcextract.core.detect

/**
 * Detects the FreeArc "ARC" container format by its magic signature,
 * `41 72 43 01` ("ArC" + 0x01). This is the exact byte sequence confirmed
 * against a real FitGirl-style `.bin` payload (see docs/ANALYSIS.md §1).
 *
 * Only the signature is decoded here; structural parsing (HEADER /
 * DIRECTORY / FOOTER blocks, file listing) is done natively in
 * `native/arc/` because it must run against multi-gigabyte files via
 * bounded reads, not JVM byte arrays.
 */
object ArcDetector {

    val MAGIC: ByteArray = byteArrayOf(0x41, 0x72, 0x43, 0x01) // "ArC" + version marker

    data class Result(
        val isArcArchive: Boolean,
        /**
         * True when the same 4-byte signature reappears within [headBytes].
         * This is only a soft confidence signal, not a structural
         * guarantee: FreeArc's real footer locator (`FindFooterDescriptor`
         * in the vendored Unarc source) finds its local descriptor by
         * scanning raw bytes for this exact pattern and can match
         * incidentally inside compressed/binary payload, which is why it
         * always re-validates every candidate against a CRC before trusting
         * it (see native/arc/, which mirrors that logic for real parsing).
         */
        val hasRepeatedLocalDescriptor: Boolean,
    )

    fun detect(headBytes: ByteArray): Result {
        if (headBytes.size < MAGIC.size || !headBytes.regionMatches(0, MAGIC)) {
            return Result(isArcArchive = false, hasRepeatedLocalDescriptor = false)
        }
        val secondOccurrence = headBytes.indexOfFrom(MAGIC, start = MAGIC.size)
        return Result(isArcArchive = true, hasRepeatedLocalDescriptor = secondOccurrence >= 0)
    }

    private fun ByteArray.regionMatches(offset: Int, other: ByteArray): Boolean {
        if (offset + other.size > size) return false
        for (i in other.indices) if (this[offset + i] != other[i]) return false
        return true
    }

    private fun ByteArray.indexOfFrom(needle: ByteArray, start: Int): Int {
        if (needle.isEmpty() || start + needle.size > size) return -1
        outer@ for (i in start..(size - needle.size)) {
            for (j in needle.indices) {
                if (this[i + j] != needle[j]) continue@outer
            }
            return i
        }
        return -1
    }
}
