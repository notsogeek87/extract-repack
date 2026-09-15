package eu.lielu.arcextract.core.backend

import com.google.common.truth.Truth.assertThat
import eu.lielu.arcextract.core.model.ArchiveEntry
import org.junit.jupiter.api.Test
import java.io.OutputStream

private class FakeBackend(override val id: String, private val handles: Boolean) : ArchiveBackend {
    override fun canHandle(sourcePath: String, headBytes: ByteArray) = handles
    override fun list(sourcePath: String): List<ArchiveEntry> = emptyList()
    override fun extract(sourcePath: String, entry: ArchiveEntry, destination: OutputStream) =
        ExtractionOutcome(0, true)
}

class BackendRegistryTest {

    @Test
    fun `returns the first backend that claims the file`() {
        val registry = BackendRegistry(
            listOf(
                FakeBackend("a", handles = false),
                FakeBackend("b", handles = true),
                FakeBackend("c", handles = true),
            ),
        )
        val found = registry.findBackend("setup.exe", ByteArray(0))
        assertThat(found?.id).isEqualTo("b")
    }

    @Test
    fun `returns null when nothing claims the file`() {
        val registry = BackendRegistry(listOf(FakeBackend("a", handles = false)))
        assertThat(registry.findBackend("mystery.bin", ByteArray(0))).isNull()
    }

    @Test
    fun `byId looks up a specific backend`() {
        val registry = BackendRegistry(listOf(FakeBackend("srep", handles = false)))
        assertThat(registry.byId("srep")).isNotNull()
        assertThat(registry.byId("xtool")).isNull()
    }
}
