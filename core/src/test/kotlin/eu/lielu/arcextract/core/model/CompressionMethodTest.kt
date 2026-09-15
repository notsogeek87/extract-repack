package eu.lielu.arcextract.core.model

import com.google.common.truth.Truth.assertThat
import org.junit.jupiter.api.Test

class CompressionMethodTest {

    @Test
    fun `store is the only method marked extractable in this build`() {
        assertThat(CompressionMethods.fromId("store").status).isEqualTo(SupportStatus.EXTRACTABLE)
        assertThat(CompressionMethods.fromId("stored").status).isEqualTo(SupportStatus.EXTRACTABLE)
    }

    @Test
    fun `lzma and ppmd are recognized but only planned, never falsely advertised as working`() {
        assertThat(CompressionMethods.fromId("lzma").status).isEqualTo(SupportStatus.PLANNED)
        assertThat(CompressionMethods.fromId("lzma2").status).isEqualTo(SupportStatus.PLANNED)
        assertThat(CompressionMethods.fromId("ppmd").status).isEqualTo(SupportStatus.PLANNED)
        assertThat(CompressionMethods.fromId("rep").status).isEqualTo(SupportStatus.PLANNED)
    }

    @Test
    fun `srep is explicitly unsupported with the exact spec wording`() {
        val info = CompressionMethods.fromId("srep")
        assertThat(info.status).isEqualTo(SupportStatus.UNSUPPORTED)
        assertThat(info.reason).contains("SREP")
        assertThat(info.reason).contains("Android indisponible")
    }

    @Test
    fun `xtool is explicitly unsupported`() {
        assertThat(CompressionMethods.fromId("xtool").status).isEqualTo(SupportStatus.UNSUPPORTED)
    }

    @Test
    fun `unknown method ids fall back to a generic unsupported entry rather than crashing`() {
        val info = CompressionMethods.fromId("some-future-codec")
        assertThat(info.status).isEqualTo(SupportStatus.UNSUPPORTED)
        assertThat(info.id).isEqualTo("some-future-codec")
    }

    @Test
    fun `method id lookup is case insensitive`() {
        assertThat(CompressionMethods.fromId("LZMA").id).isEqualTo(CompressionMethods.fromId("lzma").id)
    }
}
