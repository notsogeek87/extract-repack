package eu.lielu.arcextract.extraction

import android.content.Context
import android.net.Uri
import eu.lielu.arcextract.backend.FreeArcBackend
import eu.lielu.arcextract.core.model.ArchiveEntry
import eu.lielu.arcextract.core.security.PathSecurity
import eu.lielu.arcextract.saf.SafFileAccess
import eu.lielu.arcextract.viewmodel.ExtractionProgress
import eu.lielu.arcextract.viewmodel.ExtractionSummary
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

/**
 * Drives selective extraction of already-listed entries: security
 * validation (defense in depth even though the tree the UI built from
 * already only contains validated paths), destination creation via SAF,
 * real progress/speed/ETA from measured elapsed time and bytes written,
 * and cooperative cancellation via normal coroutine cancellation (the
 * caller cancels the `Job` this runs in; [ensureActive] turns that into a
 * clean stop at the next per-file boundary rather than a half-written
 * file — see docs/ANALYSIS.md "Pouvoir annuler proprement").
 *
 * An entry this build cannot actually decode is recorded in
 * [ExtractionSummary.skipped] with its exact reason and extraction
 * continues with the rest of the selection, rather than aborting the
 * whole run over one unsupported file.
 */
class ExtractionEngine(private val context: Context) {

    private val backend = FreeArcBackend()

    suspend fun extract(
        installerUri: Uri,
        selectedEntries: List<ArchiveEntry>,
        destinationTreeUri: Uri,
        onProgress: (ExtractionProgress) -> Unit,
    ): ExtractionSummary {
        val startTime = System.currentTimeMillis()
        val totalBytes = selectedEntries.sumOf { it.uncompressedSize ?: 0L }
        val skipped = mutableListOf<Pair<String, String>>()
        var bytesDone = 0L
        var filesDone = 0

        val descriptor = SafFileAccess.openReadDescriptor(context, installerUri)
        try {
            val fdAsSource = descriptor.fd.toString()

            for (entry in selectedEntries) {
                currentCoroutineContext().ensureActive() // cooperative cancellation point

                val validated = PathSecurity.validate(entry.path)
                if (validated !is PathSecurity.ValidationResult.Valid) {
                    skipped += entry.path to "Chemin refusé pour raison de sécurité"
                    continue
                }
                if (!entry.isExtractable) {
                    skipped += entry.path to entry.compressionMethod.reason
                    continue
                }

                val destinationFile = SafFileAccess.createDestinationFile(
                    context,
                    destinationTreeUri,
                    validated.normalizedPath,
                )
                val outcome = context.contentResolver.openOutputStream(destinationFile.uri)?.use { out ->
                    backend.extract(fdAsSource, entry, out)
                } ?: run {
                    skipped += entry.path to "Impossible d'ouvrir le fichier de destination"
                    null
                } ?: continue

                bytesDone += outcome.bytesWritten
                filesDone += 1

                val elapsedSeconds = (System.currentTimeMillis() - startTime) / 1000.0
                val speed = if (elapsedSeconds > 0) bytesDone / elapsedSeconds else 0.0
                val eta = if (speed > 0) ((totalBytes - bytesDone) / speed).toLong() else null

                onProgress(
                    ExtractionProgress(
                        currentFileName = entry.name,
                        filesDone = filesDone,
                        filesTotal = selectedEntries.size,
                        bytesDone = bytesDone,
                        bytesTotal = totalBytes,
                        bytesPerSecond = speed,
                        etaSeconds = eta,
                    ),
                )
            }
        } finally {
            descriptor.close()
        }

        return ExtractionSummary(
            fileCount = filesDone,
            totalBytes = bytesDone,
            durationMillis = System.currentTimeMillis() - startTime,
            skipped = skipped,
        )
    }
}
