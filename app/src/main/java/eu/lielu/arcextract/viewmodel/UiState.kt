package eu.lielu.arcextract.viewmodel

import android.net.Uri
import eu.lielu.arcextract.core.model.UserMessage
import eu.lielu.arcextract.core.tree.FileTreeNode

/** Top-level screen state — see docs/ANALYSIS.md "INTERFACE" for the flow this mirrors. */
sealed interface Screen {
    data object Picker : Screen
    data object Analyzing : Screen
    data class Tree(val analysis: ArchiveAnalysis) : Screen
    data class Extracting(val progress: ExtractionProgress) : Screen
    data class Done(val summary: ExtractionSummary) : Screen
    data class Failure(val message: UserMessage, val detail: String? = null) : Screen
}

data class ArchiveAnalysis(
    val installerUri: Uri,
    val installerDisplayName: String,
    val binFileNames: List<String>,
    /** Same order as [binFileNames] — the archive's actual data lives here, not behind [installerUri]. */
    val binFileUris: List<Uri>,
    val totalDataBytes: Long,
    val root: FileTreeNode,
    val selection: Set<String>,
    val messages: List<UserMessage>,
    val searchQuery: String = "",
)

data class ExtractionProgress(
    val currentFileName: String,
    val filesDone: Int,
    val filesTotal: Int,
    val bytesDone: Long,
    val bytesTotal: Long,
    val bytesPerSecond: Double,
    val etaSeconds: Long?,
    val cancelRequested: Boolean = false,
)

data class ExtractionSummary(
    val fileCount: Int,
    val totalBytes: Long,
    val durationMillis: Long,
    val skipped: List<Pair<String, String>>, // path -> human reason (e.g. unsupported codec)
)
