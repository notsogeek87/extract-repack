package eu.lielu.arcextract.core.estimate

/**
 * Estimates whether a selected set of files will fit before extraction
 * starts. Sizes come from the archive's own metadata (uncompressed size
 * field), which can be wrong for a corrupted or adversarial archive — this
 * is a pre-flight estimate to fail fast with a clear message, not a
 * substitute for handling a real "disk full" error mid-extraction.
 */
object DiskSpaceEstimator {

    /** Extra headroom requested on top of the raw byte sum, to absorb filesystem block overhead and metadata. */
    const val DEFAULT_SAFETY_MARGIN_RATIO = 0.02

    data class Estimate(
        val requiredBytes: Long,
        val marginBytes: Long,
        val availableBytes: Long,
        val sufficient: Boolean,
    )

    fun estimate(
        totalUncompressedBytes: Long,
        availableBytes: Long,
        safetyMarginRatio: Double = DEFAULT_SAFETY_MARGIN_RATIO,
    ): Estimate {
        require(totalUncompressedBytes >= 0) { "totalUncompressedBytes must not be negative" }
        require(safetyMarginRatio >= 0) { "safetyMarginRatio must not be negative" }
        val margin = (totalUncompressedBytes * safetyMarginRatio).toLong()
        val required = totalUncompressedBytes + margin
        return Estimate(
            requiredBytes = required,
            marginBytes = margin,
            availableBytes = availableBytes,
            sufficient = availableBytes >= required,
        )
    }
}
