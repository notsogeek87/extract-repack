package eu.lielu.arcextract.saf

import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile

/**
 * All Storage Access Framework access lives here — nothing else in the app
 * touches `ContentResolver`/`DocumentFile` directly (see
 * docs/ANALYSIS.md "GESTION DES FICHIERS": never assume a plain
 * `/storage/emulated/0/...` path is available).
 */
object SafFileAccess {

    /** Reads at most [maxBytes] from the start of [uri] — used for content-based format detection, never the whole file. */
    fun readHead(context: Context, uri: Uri, maxBytes: Int): ByteArray {
        context.contentResolver.openInputStream(uri).use { stream ->
            requireNotNull(stream) { "Impossible d'ouvrir le flux pour $uri" }
            val buffer = ByteArray(maxBytes)
            var totalRead = 0
            while (totalRead < maxBytes) {
                val n = stream.read(buffer, totalRead, maxBytes - totalRead)
                if (n < 0) break
                totalRead += n
            }
            return if (totalRead == maxBytes) buffer else buffer.copyOf(totalRead)
        }
    }

    /**
     * Opens [uri] for reading and returns the [ParcelFileDescriptor] —
     * callers pass `.fd` to [eu.lielu.arcextract.jni.ArcNative] and MUST
     * close the returned descriptor themselves once done (native code
     * only ever dup()s it, see native/arc/file_source.cpp).
     */
    fun openReadDescriptor(context: Context, uri: Uri): ParcelFileDescriptor {
        return context.contentResolver.openFileDescriptor(uri, "r")
            ?: throw java.io.IOException("Le fournisseur de documents n'a pas pu ouvrir $uri en lecture")
    }

    fun displayName(context: Context, uri: Uri): String {
        return DocumentFile.fromSingleUri(context, uri)?.name ?: uri.lastPathSegment ?: uri.toString()
    }

    /** Every direct child of a tree Uri (non-recursive) — used to find sibling `.bin` files next to a picked `setup.exe`. */
    fun listChildren(context: Context, treeUri: Uri): List<DocumentFile> {
        val dir = DocumentFile.fromTreeUri(context, treeUri) ?: return emptyList()
        return dir.listFiles().toList()
    }

    /**
     * Creates (or reuses) the destination file for [relativePath] under
     * [destinationTreeUri], creating any missing intermediate directories.
     * [relativePath] must already be validated by
     * [eu.lielu.arcextract.core.security.PathSecurity] — this function
     * does not re-check for ".." itself, it only ever calls
     * `DocumentFile.createDirectory`/`createFile`, which cannot escape the
     * tree they are called on regardless.
     */
    fun createDestinationFile(context: Context, destinationTreeUri: Uri, relativePath: String, mimeType: String = "application/octet-stream"): DocumentFile {
        var dir = DocumentFile.fromTreeUri(context, destinationTreeUri)
            ?: throw java.io.IOException("Dossier de destination invalide")
        val segments = relativePath.split('/')
        for (i in 0 until segments.size - 1) {
            val name = segments[i]
            dir = dir.findFile(name)?.takeIf { it.isDirectory } ?: dir.createDirectory(name)
                ?: throw java.io.IOException("Impossible de créer le dossier $name")
        }
        val fileName = segments.last()
        val existing = dir.findFile(fileName)
        if (existing != null && existing.isFile) {
            // Collision policy: overwrite in place rather than silently
            // renaming or skipping, since the user explicitly selected
            // this file for extraction into this destination.
            return existing
        }
        return dir.createFile(mimeType, fileName)
            ?: throw java.io.IOException("Impossible de créer le fichier $fileName")
    }

    /**
     * Free space on the volume backing [treeUri], via the document
     * provider's own `Root.COLUMN_AVAILABLE_BYTES` when it reports one.
     * Not every provider does (some legitimately don't know, some report
     * a stale/approximate value) — callers must treat a non-positive
     * result as "unknown" rather than "definitely full", matching
     * docs/ANALYSIS.md's "calculer autant que possible" wording: this is
     * a best-effort pre-flight check, not a guarantee.
     */
    fun availableBytes(context: Context, treeUri: Uri): Long {
        return try {
            val docId = DocumentsContract.getTreeDocumentId(treeUri)
            val rootId = docId.substringBefore(':')
            val rootUri = DocumentsContract.buildRootUri(treeUri.authority, rootId)
            context.contentResolver.query(
                rootUri,
                arrayOf(DocumentsContract.Root.COLUMN_AVAILABLE_BYTES),
                null, null, null,
            )?.use { cursor ->
                if (cursor.moveToFirst()) cursor.getLong(0) else 0L
            } ?: 0L
        } catch (_: Exception) {
            0L
        }
    }
}
