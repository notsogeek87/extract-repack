package eu.lielu.arcextract.core.detect

import com.google.common.truth.Truth.assertThat
import org.junit.jupiter.api.Test

class SiblingBinFinderTest {

    @Test
    fun `finds and naturally sorts fg-style bin files`() {
        val names = listOf("setup.exe", "fg-02.bin", "fg-10.bin", "fg-01.bin", "readme.txt")
        val result = SiblingBinFinder.findBinFiles(names)
        assertThat(result).containsExactly("fg-01.bin", "fg-02.bin", "fg-10.bin").inOrder()
    }

    @Test
    fun `is case insensitive on the extension`() {
        val names = listOf("data.BIN", "data2.Bin")
        assertThat(SiblingBinFinder.findBinFiles(names)).hasSize(2)
    }

    @Test
    fun `returns empty when no bin files are present`() {
        assertThat(SiblingBinFinder.findBinFiles(listOf("setup.exe", "readme.txt"))).isEmpty()
    }

    @Test
    fun `handles the exact fixture from the product spec`() {
        val names = listOf("setup.exe", "fg-01.bin", "fg-02.bin", "fg-03.bin")
        assertThat(SiblingBinFinder.findBinFiles(names))
            .containsExactly("fg-01.bin", "fg-02.bin", "fg-03.bin")
            .inOrder()
    }
}
