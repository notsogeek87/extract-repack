package eu.lielu.arcextract.core.detect

import com.google.common.truth.Truth.assertThat
import org.junit.jupiter.api.Test

class InnoSetupDetectorTest {

    private fun fakeInnoExe(version: String, unicode: Boolean): ByteArray {
        val padding = ByteArray(64) { 0x00 }
        val marker = "Inno Setup Setup Data ($version)${if (unicode) " (u)" else ""}"
        return "MZ".toByteArray(Charsets.US_ASCII) + padding + marker.toByteArray(Charsets.US_ASCII)
    }

    @Test
    fun `detects a real-looking Inno Setup header and extracts version`() {
        val bytes = fakeInnoExe("5.5.0.1", unicode = true)
        val result = InnoSetupDetector.detect(bytes)
        assertThat(result.isInnoSetup).isTrue()
        assertThat(result.version).isEqualTo("5.5.0.1")
        assertThat(result.isUnicode).isTrue()
    }

    @Test
    fun `detects non-unicode variant`() {
        val bytes = fakeInnoExe("6.2.2", unicode = false)
        val result = InnoSetupDetector.detect(bytes)
        assertThat(result.isInnoSetup).isTrue()
        assertThat(result.version).isEqualTo("6.2.2")
        assertThat(result.isUnicode).isFalse()
    }

    @Test
    fun `rejects a file without the MZ PE magic`() {
        val bytes = "Inno Setup Setup Data (5.5.0.1)".toByteArray(Charsets.US_ASCII)
        val result = InnoSetupDetector.detect(bytes)
        assertThat(result.isInnoSetup).isFalse()
    }

    @Test
    fun `rejects a PE file without the marker string (extension alone is not enough)`() {
        val bytes = "MZ".toByteArray(Charsets.US_ASCII) + ByteArray(256)
        val result = InnoSetupDetector.detect(bytes)
        assertThat(result.isInnoSetup).isFalse()
        assertThat(result.version).isNull()
    }

    @Test
    fun `handles an empty buffer without throwing`() {
        val result = InnoSetupDetector.detect(ByteArray(0))
        assertThat(result.isInnoSetup).isFalse()
    }
}
