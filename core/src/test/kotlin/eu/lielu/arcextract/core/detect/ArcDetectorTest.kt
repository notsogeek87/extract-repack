package eu.lielu.arcextract.core.detect

import com.google.common.truth.Truth.assertThat
import org.junit.jupiter.api.Test

class ArcDetectorTest {

    /**
     * Exact leading bytes of the real fg-01.bin payload reported by the
     * user (see docs/ANALYSIS.md §1):
     * 41 72 43 01 00 00 06 07 41 72 43 01 02 73 74 6f
     */
    private val realFgBinHead = byteArrayOf(
        0x41, 0x72, 0x43, 0x01, 0x00, 0x00, 0x06, 0x07,
        0x41, 0x72, 0x43, 0x01, 0x02, 0x73, 0x74, 0x6f,
    )

    @Test
    fun `detects the real fg-01_bin header as an ARC archive`() {
        val result = ArcDetector.detect(realFgBinHead)
        assertThat(result.isArcArchive).isTrue()
    }

    @Test
    fun `finds the repeated local descriptor signature in the real header`() {
        val result = ArcDetector.detect(realFgBinHead)
        assertThat(result.hasRepeatedLocalDescriptor).isTrue()
    }

    @Test
    fun `rejects a plain zip header`() {
        val zipMagic = byteArrayOf(0x50, 0x4b, 0x03, 0x04)
        val result = ArcDetector.detect(zipMagic)
        assertThat(result.isArcArchive).isFalse()
    }

    @Test
    fun `rejects a buffer shorter than the magic`() {
        val result = ArcDetector.detect(byteArrayOf(0x41, 0x72))
        assertThat(result.isArcArchive).isFalse()
    }

    @Test
    fun `does not falsely report a repeated descriptor when there is only one`() {
        val single = ArcDetector.MAGIC + ByteArray(32) { 0x00 }
        val result = ArcDetector.detect(single)
        assertThat(result.isArcArchive).isTrue()
        assertThat(result.hasRepeatedLocalDescriptor).isFalse()
    }
}
