package eu.lielu.arcextract.jni

/**
 * Kotlin side of the JNI bridge to `native/jni/arc_extract_jni.cpp`, which
 * links `native/arc/` (see docs/ANALYSIS.md §6). This is the only class in
 * the app that calls `System.loadLibrary` — every other layer talks to
 * [ArcNative], never to JNI directly.
 *
 * Every method here takes raw POSIX file descriptors ([IntArray]), not
 * paths or `Uri`s: SAF documents (especially anything other than plain
 * local storage) are not guaranteed to have a real filesystem path, but
 * `ContentResolver.openFileDescriptor(uri, "r")` always works for a
 * readable document and hands back a `ParcelFileDescriptor` whose `.fd`
 * this bridge can open directly (native/arc/file_source.cpp dup()s it, so
 * ownership/closing stays with the Kotlin caller). This class itself
 * never touches `ContentResolver` — see `eu.lielu.arcextract.saf` for
 * that layer.
 *
 * [fds] is always the *ordered* list of a single logical archive's parts:
 * one fd for a plain single-file archive, or one fd per sibling `.bin`
 * file (`fg-01.bin`, `fg-02.bin`, ...) for an Inno Setup style
 * multi-part installer, where the real FreeArc container is the
 * concatenation of all parts, not any one of them alone (see
 * docs/ANALYSIS.md §1 and native/arc/file_source.h).
 */
object ArcNative {
    init {
        System.loadLibrary("arcextract_jni")
    }

    /**
     * Lists every file/directory entry in the FreeArc "ArC" container
     * spanning [fds], following the FOOTER → DIRECTORY chain
     * (native/arc/arc_reader.cpp) without decompressing file contents.
     *
     * @throws UnsupportedCompressorException if a DIRECTORY or FOOTER
     *   control block itself (not a file's content — see class doc on
     *   [NativeArcEntry.compressorId]) uses a codec this build cannot
     *   decode.
     * @throws ArcCorruptedException if a CRC check fails anywhere while
     *   reading the container structure.
     * @throws java.io.IOException for anything else (file not found, short read...).
     */
    external fun listArc(fds: IntArray): Array<NativeArcEntry>

    /**
     * Reads exactly [length] raw bytes from the logical archive spanning
     * [fds] at [absolutePos], with no decompression of any kind — this is
     * the "store" (uncompressed) extraction path exposed as a generic
     * primitive so the Kotlin side (see `FreeArcBackend.extract`) can
     * chunk it into an arbitrary [java.io.OutputStream], including a
     * SAF-backed one from `ContentResolver.openOutputStream()`, without
     * ever holding a whole entry in memory at once.
     */
    external fun readRawChunk(fds: IntArray, absolutePos: Long, length: Int): ByteArray

    /** Quick, cheap check of just the 4-byte "ArC" signature — no full parse. */
    external fun isArcSignature(headBytes: ByteArray): Boolean
}

/**
 * Native-side counterpart of [eu.lielu.arcextract.core.model.ArchiveEntry],
 * constructed directly from JNI (see `arc_extract_jni.cpp`). [compressorId]
 * here names the codec of the *solid/data block this file lives in* — the
 * app maps it through
 * [eu.lielu.arcextract.core.model.CompressionMethods.fromId] to decide
 * whether extraction is actually possible.
 */
class NativeArcEntry(
    @JvmField val path: String,
    @JvmField val isDirectory: Boolean,
    @JvmField val uncompressedSize: Long,
    @JvmField val crc: Long,
    @JvmField val compressorId: String,
    @JvmField val dataBlockAbsolutePos: Long,
)

class UnsupportedCompressorException(val compressorId: String) :
    Exception("Unsupported compressor: $compressorId")

class ArcCorruptedException(message: String) : Exception(message)
