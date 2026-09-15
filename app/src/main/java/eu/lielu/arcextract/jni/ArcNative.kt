package eu.lielu.arcextract.jni

/**
 * Kotlin side of the JNI bridge to `native/jni/arc_extract_jni.cpp`, which
 * links `native/arc/` (see docs/ANALYSIS.md §6). This is the only class in
 * the app that calls `System.loadLibrary` — every other layer talks to
 * [ArcNative], never to JNI directly.
 *
 * Every method here takes a raw POSIX file descriptor ([Int]), not a path
 * or a `Uri`: SAF documents (especially anything other than plain local
 * storage) are not guaranteed to have a real filesystem path, but
 * `ContentResolver.openFileDescriptor(uri, "r")` always works for a
 * readable document and hands back a `ParcelFileDescriptor` whose `.fd`
 * this bridge can open directly (native/arc/file_source.cpp dup()s it, so
 * ownership/closing stays with the Kotlin caller). This class itself
 * never touches `ContentResolver` — see `eu.lielu.arcextract.saf` for
 * that layer.
 */
object ArcNative {
    init {
        System.loadLibrary("arcextract_jni")
    }

    /**
     * Lists every file/directory entry in the FreeArc "ArC" container
     * behind [fd], following the FOOTER → DIRECTORY chain
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
    external fun listArc(fd: Int): Array<NativeArcEntry>

    /**
     * Reads exactly [length] raw bytes from the file behind [fd] at
     * [absolutePos], with no decompression of any kind — this is the
     * "store" (uncompressed) extraction path exposed as a generic
     * primitive so the Kotlin side (see `FreeArcBackend.extract`) can
     * chunk it into an arbitrary [java.io.OutputStream], including a
     * SAF-backed one from `ContentResolver.openOutputStream()`, without
     * ever holding a whole entry in memory at once.
     */
    external fun readRawChunk(fd: Int, absolutePos: Long, length: Int): ByteArray

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
