package eu.lielu.arcextract.core.detect

import eu.lielu.arcextract.core.model.UserMessage

/**
 * Combines [InnoSetupDetector], [SiblingBinFinder] and [ArcDetector] into
 * the single "what did we find when the user picked a file/folder" summary
 * the first screen needs. Pure function of already-read bytes and folder
 * listings — no I/O here, so it is fully unit-testable and reusable from
 * both the SAF (`ACTION_OPEN_DOCUMENT`) and folder-tree (`ACTION_OPEN_DOCUMENT_TREE`) flows.
 */
object InstallerAnalyzer {

    data class Analysis(
        val setupExeHead: InnoSetupDetector.Result,
        val binFileNames: List<String>,
        val binFileHeads: Map<String, ArcDetector.Result>,
        val messages: List<UserMessage>,
    )

    fun analyze(
        setupExeHeadBytes: ByteArray,
        filesInSameFolder: List<String>,
        binFileHeadBytes: Map<String, ByteArray>,
    ): Analysis {
        val innoResult = InnoSetupDetector.detect(setupExeHeadBytes)
        val binNames = SiblingBinFinder.findBinFiles(filesInSameFolder)
        val binHeads = binNames.associateWith { name ->
            binFileHeadBytes[name]?.let(ArcDetector::detect)
                ?: ArcDetector.Result(isArcArchive = false, hasRepeatedLocalDescriptor = false)
        }

        val messages = buildList {
            if (innoResult.isInnoSetup) {
                add(UserMessage.InnoSetupDetected)
            } else {
                add(UserMessage.NotInnoSetup)
            }
            if (binNames.isEmpty()) {
                add(UserMessage.BinFilesMissing)
            } else {
                add(UserMessage.BinFilesFound(binNames.size))
            }
            if (binHeads.values.any { it.isArcArchive }) {
                add(UserMessage.ArcArchiveDetected)
            }
        }

        return Analysis(
            setupExeHead = innoResult,
            binFileNames = binNames,
            binFileHeads = binHeads,
            messages = messages,
        )
    }
}
