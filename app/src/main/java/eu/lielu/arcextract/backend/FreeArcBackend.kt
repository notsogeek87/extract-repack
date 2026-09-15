package eu.lielu.arcextract.backend

import eu.lielu.arcextract.core.backend.ArchiveBackend
import eu.lielu.arcextract.core.backend.ExtractionOutcome
import eu.lielu.arcextract.core.backend.UnsupportedEntryException
import eu.lielu.arcextract.core.model.ArchiveEntry
import eu.lielu.arcextract.core.model.CompressionMethods
import eu.lielu.arcextract.core.model.EntryType
import eu.lielu.arcextract.jni.ArcCorruptedException
import eu.lielu.arcextract.jni.ArcNative
import eu.lielu.arcextract.jni.UnsupportedCompressorException
import java.io.OutputStream
import java.util.zip.CRC32

/**
 * FreeArc "ArC" container backend (native/arc/, see docs/ANALYSIS.md §3).
 * Listing works for the whole container structure regardless of which
 * codec each solid block declares — only actually *extracting* an entry
 * requires a working decoder for its specific `compressionMethod`, and
 * today that is only true for `store` (uncompressed). Everything else
 * throws [UnsupportedEntryException] with the precise reason from
 * [CompressionMethods], never a silent no-op or partial write.
 */
class FreeArcBackend : ArchiveBackend {
    override val id: String = "freearc"

    override fun canHandle(source: String, headBytes: ByteArray): Boolean =
        ArcNative.isArcSignature(headBytes)

    /** @param source decimal file-descriptor number, as opened by the caller via SAF — see [ArchiveBackend]. */
    override fun list(source: String): List<ArchiveEntry> {
        val fd = source.toIntOrNull()
            ?: throw UnsupportedEntryException("Descripteur de fichier invalide : $source")
        val nativeEntries = try {
            ArcNative.listArc(fd)
        } catch (e: UnsupportedCompressorException) {
            // Even *listing* needs a codec when a DIRECTORY/FOOTER control
            // block itself is compressed with something other than store.
            throw UnsupportedEntryException(CompressionMethods.fromId(e.compressorId).reason)
        } catch (e: ArcCorruptedException) {
            throw UnsupportedEntryException("Archive corrompue : ${e.message}")
        }

        return nativeEntries.map { n ->
            ArchiveEntry(
                path = n.path,
                type = if (n.isDirectory) EntryType.DIRECTORY else EntryType.FILE,
                compressedSize = null,
                uncompressedSize = n.uncompressedSize,
                compressionMethod = CompressionMethods.fromId(n.compressorId),
                // A backend operating on a bare fd has no filename to put
                // here (see [ArchiveBackend] on why `source` is an fd, not
                // a path); the UI layer, which does know the picked
                // document's display name, is expected to overwrite this
                // per docs/BUILDING.md rather than trust it from here.
                sourceArchive = source,
                crc32 = n.crc,
                backendData = n.dataBlockAbsolutePos.toString(),
            )
        }
    }

    override fun extract(source: String, entry: ArchiveEntry, destination: OutputStream): ExtractionOutcome {
        if (!entry.isExtractable) {
            throw UnsupportedEntryException(entry.compressionMethod.reason)
        }
        val fd = source.toIntOrNull()
            ?: throw UnsupportedEntryException("Descripteur de fichier invalide : $source")
        val dataBlockPos = entry.backendData?.toLongOrNull()
            ?: throw UnsupportedEntryException("Position introuvable pour ${entry.path} (archive non relistée ?)")
        val totalSize = entry.uncompressedSize
            ?: throw UnsupportedEntryException("Taille inconnue pour ${entry.path}")

        val crc = CRC32()
        var written = 0L
        while (written < totalSize) {
            val remaining = totalSize - written
            val chunkLength = minOf(remaining, CHUNK_BYTES).toInt()
            val chunk = ArcNative.readRawChunk(fd, dataBlockPos + written, chunkLength)
            destination.write(chunk)
            crc.update(chunk)
            written += chunk.size
        }

        val crcVerified = entry.crc32 == null || (crc.value and 0xFFFFFFFFL) == (entry.crc32 and 0xFFFFFFFFL)
        if (!crcVerified) {
            throw UnsupportedEntryException("Archive corrompue : CRC invalide pour ${entry.path}")
        }
        return ExtractionOutcome(bytesWritten = written, crcVerified = true)
    }

    private companion object {
        const val CHUNK_BYTES = 4L * 1024 * 1024
    }
}
