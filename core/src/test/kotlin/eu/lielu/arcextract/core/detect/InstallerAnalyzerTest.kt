package eu.lielu.arcextract.core.detect

import com.google.common.truth.Truth.assertThat
import eu.lielu.arcextract.core.model.UserMessage
import org.junit.jupiter.api.Test

class InstallerAnalyzerTest {

    private val realFgBinHead = byteArrayOf(
        0x41, 0x72, 0x43, 0x01, 0x00, 0x00, 0x06, 0x07,
        0x41, 0x72, 0x43, 0x01, 0x02, 0x73, 0x74, 0x6f,
    )

    private fun fakeInnoExe(version: String) =
        "MZ".toByteArray(Charsets.US_ASCII) + ByteArray(32) +
            "Inno Setup Setup Data ($version) (u)".toByteArray(Charsets.US_ASCII)

    @Test
    fun `reproduces the exact scenario from the product spec`() {
        val analysis = InstallerAnalyzer.analyze(
            setupExeHeadBytes = fakeInnoExe("5.5.0.1"),
            filesInSameFolder = listOf("setup.exe", "fg-01.bin", "fg-02.bin", "fg-03.bin"),
            binFileHeadBytes = mapOf(
                "fg-01.bin" to realFgBinHead,
                "fg-02.bin" to realFgBinHead,
                "fg-03.bin" to realFgBinHead,
            ),
        )

        assertThat(analysis.setupExeHead.isInnoSetup).isTrue()
        assertThat(analysis.binFileNames).containsExactly("fg-01.bin", "fg-02.bin", "fg-03.bin").inOrder()
        assertThat(analysis.messages).contains(UserMessage.InnoSetupDetected)
        assertThat(analysis.messages).contains(UserMessage.BinFilesFound(3))
        assertThat(analysis.messages).contains(UserMessage.ArcArchiveDetected)
    }

    @Test
    fun `reports missing bin files when none are present`() {
        val analysis = InstallerAnalyzer.analyze(
            setupExeHeadBytes = fakeInnoExe("6.2.2"),
            filesInSameFolder = listOf("setup.exe"),
            binFileHeadBytes = emptyMap(),
        )
        assertThat(analysis.messages).contains(UserMessage.BinFilesMissing)
    }

    @Test
    fun `reports a non-Inno-Setup file honestly instead of guessing`() {
        val analysis = InstallerAnalyzer.analyze(
            setupExeHeadBytes = "MZ".toByteArray(Charsets.US_ASCII) + ByteArray(64),
            filesInSameFolder = emptyList(),
            binFileHeadBytes = emptyMap(),
        )
        assertThat(analysis.messages).contains(UserMessage.NotInnoSetup)
    }
}
