package eu.lielu.arcextract.core.detect

/**
 * Detects Inno Setup installers by *content*, not by file extension.
 *
 * Real Inno Setup installers are Windows PE executables (`MZ` at offset 0)
 * that embed the ASCII marker `Inno Setup Setup Data (` near their setup
 * header, followed by a version string, e.g. `Inno Setup Setup Data
 * (5.5.0.1)`. This is the same content-based heuristic long used by
 * general-purpose file-identification tools; it does not require parsing
 * the PE resource table, which makes it cheap to run against the first
 * chunk of a very large file streamed via SAF.
 *
 * This detector only answers "is this plausibly an Inno Setup installer,
 * and if so which version does it claim". Actually listing/extracting the
 * installer's contents is `innoextract`'s job (native `InnoSetupBackend`,
 * see docs/ANALYSIS.md §2) — this class never loads more than
 * [DEFAULT_SCAN_WINDOW] bytes into memory.
 */
object InnoSetupDetector {

    /** How many leading bytes callers should read before calling [detect] — 8 MiB is enough; the marker sits early. */
    const val DEFAULT_SCAN_WINDOW = 8 * 1024 * 1024
    private val MARKER = "Inno Setup Setup Data (".toByteArray(Charsets.US_ASCII)
    private val PE_MAGIC = byteArrayOf('M'.code.toByte(), 'Z'.code.toByte())

    data class Result(
        val isInnoSetup: Boolean,
        val version: String?,
        val isUnicode: Boolean,
    )

    /**
     * @param headBytes the first bytes of the candidate file, up to
     *   [DEFAULT_SCAN_WINDOW]. Callers are responsible for streaming only
     *   this much from disk/SAF — this function never reads anything
     *   itself, so it works identically whether the source is a local
     *   file, a SAF [android.content.ContentResolver] stream, or a test
     *   fixture.
     */
    fun detect(headBytes: ByteArray): Result {
        if (headBytes.size < PE_MAGIC.size || !headBytes.startsWith(PE_MAGIC)) {
            return Result(isInnoSetup = false, version = null, isUnicode = false)
        }
        val markerIndex = headBytes.indexOfSubsequence(MARKER)
        if (markerIndex < 0) {
            return Result(isInnoSetup = false, version = null, isUnicode = false)
        }
        val versionStart = markerIndex + MARKER.size
        val closeParen = headBytes.indexOf(')'.code.toByte(), from = versionStart)
        val version = if (closeParen > versionStart) {
            String(headBytes, versionStart, closeParen - versionStart, Charsets.US_ASCII)
        } else {
            null
        }
        // Recent Inno Setup versions append "(u)" for the unicode variant,
        // e.g. "Inno Setup Setup Data (5.5.0.1) (u)".
        val tail = if (closeParen in headBytes.indices) {
            String(headBytes, closeParen, minOf(8, headBytes.size - closeParen), Charsets.US_ASCII)
        } else {
            ""
        }
        val isUnicode = tail.contains("(u)")
        return Result(isInnoSetup = true, version = version, isUnicode = isUnicode)
    }

    private fun ByteArray.startsWith(prefix: ByteArray): Boolean {
        if (size < prefix.size) return false
        for (i in prefix.indices) if (this[i] != prefix[i]) return false
        return true
    }

    private fun ByteArray.indexOf(byte: Byte, from: Int): Int {
        for (i in from until size) if (this[i] == byte) return i
        return -1
    }

    private fun ByteArray.indexOfSubsequence(needle: ByteArray): Int {
        if (needle.isEmpty() || needle.size > size) return -1
        outer@ for (i in 0..(size - needle.size)) {
            for (j in needle.indices) {
                if (this[i + j] != needle[j]) continue@outer
            }
            return i
        }
        return -1
    }
}
