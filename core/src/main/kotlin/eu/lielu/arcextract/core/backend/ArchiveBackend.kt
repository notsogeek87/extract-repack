package eu.lielu.arcextract.core.backend

import eu.lielu.arcextract.core.model.ArchiveEntry
import java.io.OutputStream

/**
 * One decoder capable of handling a specific container/compression layer
 * (Inno Setup, FreeArc/ARC, ...). Kept here as a pure contract — no
 * Android or JNI types — so it stays unit-testable on the JVM; concrete
 * implementations that need the native engine live in the `:app` module
 * (only it can load the compiled `.so`), see docs/ANALYSIS.md §6.
 *
 * A backend must never claim to extract something it cannot: if
 * [canHandle] returns true but the specific entry uses a compression
 * method this build does not implement, [extract] throws
 * [UnsupportedEntryException] rather than writing partial/garbage output.
 */
interface ArchiveBackend {
    val id: String

    /**
     * [source] is a backend-defined identifier for where to read from —
     * deliberately opaque at this layer since what a backend needs varies:
     * a real filesystem path works for some, but SAF documents are not
     * guaranteed to have one, so the native FreeArc backend instead
     * expects a decimal POSIX file-descriptor number as a string (see
     * `FreeArcBackend` in the `:app` module, and
     * `ContentResolver.openFileDescriptor` on the caller's side). Kept as
     * a plain `String` rather than a sealed type to avoid over-modeling
     * this before a second real backend needs something else.
     */
    fun canHandle(source: String, headBytes: ByteArray): Boolean

    fun list(source: String): List<ArchiveEntry>

    fun extract(source: String, entry: ArchiveEntry, destination: OutputStream): ExtractionOutcome
}

data class ExtractionOutcome(
    val bytesWritten: Long,
    val crcVerified: Boolean,
)

class UnsupportedEntryException(message: String) : Exception(message)

/**
 * Picks the right backend for a given source, in a fixed priority order.
 * Returns null when nothing in [backends] claims the file — the caller
 * should then show [eu.lielu.arcextract.core.model.UserMessage.NotInnoSetup]
 * or an equivalent "format not recognized" message rather than guessing.
 */
class BackendRegistry(private val backends: List<ArchiveBackend>) {
    fun findBackend(sourcePath: String, headBytes: ByteArray): ArchiveBackend? =
        backends.firstOrNull { it.canHandle(sourcePath, headBytes) }

    fun byId(id: String): ArchiveBackend? = backends.firstOrNull { it.id == id }
}
