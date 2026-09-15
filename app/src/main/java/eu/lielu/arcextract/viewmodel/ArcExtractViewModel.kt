package eu.lielu.arcextract.viewmodel

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import eu.lielu.arcextract.backend.FreeArcBackend
import eu.lielu.arcextract.core.backend.UnsupportedEntryException
import eu.lielu.arcextract.core.detect.InnoSetupDetector
import eu.lielu.arcextract.core.detect.SiblingBinFinder
import eu.lielu.arcextract.core.estimate.DiskSpaceEstimator
import eu.lielu.arcextract.core.model.ArchiveEntry
import eu.lielu.arcextract.core.model.UserMessage
import eu.lielu.arcextract.core.security.PathSecurity
import eu.lielu.arcextract.core.tree.FileTree
import eu.lielu.arcextract.core.tree.FileTreeNode
import eu.lielu.arcextract.core.tree.SelectionState
import eu.lielu.arcextract.extraction.ExtractionEngine
import eu.lielu.arcextract.saf.SafFileAccess
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class ArcExtractViewModel(application: Application) : AndroidViewModel(application) {

    private val _screen = MutableStateFlow<Screen>(Screen.Picker)
    val screen: StateFlow<Screen> = _screen.asStateFlow()

    private val engine by lazy { ExtractionEngine(getApplication()) }
    private var extractionJob: Job? = null

    // --- Entry points from the picker screen -------------------------------

    /** ACTION_OPEN_DOCUMENT_TREE result: the recommended flow, since it also grants sibling-file discovery. */
    fun onFolderPicked(treeUri: Uri) {
        viewModelScope.launch {
            _screen.value = Screen.Analyzing
            runCatching { analyzeFolder(treeUri) }
                .onFailure { reportFailure(it) }
        }
    }

    /**
     * ACTION_OPEN_DOCUMENT result for a single `setup.exe`. SAF does not
     * generally grant access to sibling files from a single-document pick
     * (see docs/ANALYSIS.md "GESTION DES FICHIERS") — if that turns out to
     * be the case for this particular Uri, this reports
     * [UserMessage.BinFilesMissing] and the picker screen should suggest
     * "Choisir un dossier" instead, exactly as the product spec requires
     * a folder-based fallback.
     */
    fun onSetupExePicked(uri: Uri) {
        viewModelScope.launch {
            _screen.value = Screen.Analyzing
            runCatching { analyzeSingleFile(uri) }
                .onFailure { reportFailure(it) }
        }
    }

    // --- Tree screen interactions -------------------------------------------

    fun toggleNode(node: FileTreeNode) = updateTreeAnalysis { analysis ->
        analysis.copy(selection = FileTree.toggle(analysis.selection, node))
    }

    fun selectAll() = updateTreeAnalysis { analysis ->
        analysis.copy(selection = FileTree.selectAll(analysis.root))
    }

    fun deselectAll() = updateTreeAnalysis { analysis ->
        analysis.copy(selection = FileTree.deselectAll())
    }

    fun updateSearchQuery(query: String) = updateTreeAnalysis { analysis ->
        analysis.copy(searchQuery = query)
    }

    fun selectionState(node: FileTreeNode): SelectionState {
        val analysis = (_screen.value as? Screen.Tree)?.analysis ?: return SelectionState.NONE
        return FileTree.selectionState(node, analysis.selection)
    }

    // --- Extraction ----------------------------------------------------------

    fun startExtraction(destinationTreeUri: Uri) {
        val analysis = (_screen.value as? Screen.Tree)?.analysis ?: return
        val selectedEntries = analysis.selection.mapNotNull { path -> analysis.root.find(path)?.entry }
        if (selectedEntries.isEmpty()) return

        val availableBytes = SafFileAccess.availableBytes(getApplication(), destinationTreeUri)
        val totalSelectedBytes = selectedEntries.sumOf { it.uncompressedSize ?: 0L }
        if (availableBytes > 0) {
            val estimate = DiskSpaceEstimator.estimate(totalSelectedBytes, availableBytes)
            if (!estimate.sufficient) {
                _screen.value = Screen.Failure(UserMessage.InsufficientDiskSpace)
                return
            }
        }

        _screen.value = Screen.Extracting(
            ExtractionProgress(
                currentFileName = "",
                filesDone = 0,
                filesTotal = selectedEntries.size,
                bytesDone = 0,
                bytesTotal = totalSelectedBytes,
                bytesPerSecond = 0.0,
                etaSeconds = null,
            ),
        )

        extractionJob = viewModelScope.launch {
            runCatching {
                engine.extract(analysis.binFileUris, selectedEntries, destinationTreeUri) { progress ->
                    _screen.value = Screen.Extracting(progress)
                }
            }.onSuccess { summary ->
                _screen.value = Screen.Done(summary)
            }.onFailure { error ->
                if (error is kotlinx.coroutines.CancellationException) {
                    _screen.value = Screen.Picker // cancelled: back to a clean start, nothing left half-configured
                } else {
                    reportFailure(error)
                }
            }
        }
    }

    fun cancelExtraction() {
        extractionJob?.cancel()
    }

    fun reset() {
        extractionJob = null
        _screen.value = Screen.Picker
    }

    // --- Analysis pipeline -----------------------------------------------------

    private suspend fun analyzeFolder(treeUri: Uri) {
        val context = getApplication<Application>()
        val children = SafFileAccess.listChildren(context, treeUri).filter { it.isFile }
        val heads = children.associate { doc ->
            (doc.name ?: "") to SafFileAccess.readHead(context, doc.uri, InnoSetupDetector.DEFAULT_SCAN_WINDOW)
        }

        val setupCandidate = children.firstOrNull { doc ->
            InnoSetupDetector.detect(heads[doc.name ?: ""] ?: ByteArray(0)).isInnoSetup
        }
        if (setupCandidate == null) {
            _screen.value = Screen.Failure(UserMessage.NotInnoSetup)
            return
        }

        val allNames = children.mapNotNull { it.name }
        val binNames = SiblingBinFinder.findBinFiles(allNames)
        if (binNames.isEmpty()) {
            _screen.value = Screen.Failure(UserMessage.BinFilesMissing)
            return
        }

        // A repack's FreeArc archive is the *concatenation* of every sibling
        // .bin part in order (fg-01.bin, fg-02.bin, ...), not just the first
        // one — Inno Setup itself reads them back the same way. The FOOTER
        // ArcReader locates from EOF, and even a solid block's content, can
        // land in any part, so all of them must be opened together (see
        // docs/ANALYSIS.md §1, native/arc/file_source.h).
        val binByName = children.associateBy { it.name }
        val binUris = binNames.map { name ->
            binByName[name]?.uri ?: error("listed .bin file '$name' vanished from the folder listing")
        }
        val entries = listArcEntries(binUris)
        buildTreeScreen(
            installerUri = setupCandidate.uri,
            installerDisplayName = setupCandidate.name ?: "setup.exe",
            binNames = binNames,
            binUris = binUris,
            entries = entries,
        )
    }

    private suspend fun analyzeSingleFile(uri: Uri) {
        val context = getApplication<Application>()
        val head = SafFileAccess.readHead(context, uri, InnoSetupDetector.DEFAULT_SCAN_WINDOW)
        if (!InnoSetupDetector.detect(head).isInnoSetup) {
            _screen.value = Screen.Failure(UserMessage.NotInnoSetup)
            return
        }
        // No tree access from a single ACTION_OPEN_DOCUMENT pick in
        // general — see the kdoc on onSetupExePicked.
        _screen.value = Screen.Failure(UserMessage.BinFilesMissing)
    }

    private suspend fun listArcEntries(binUris: List<Uri>): List<ArchiveEntry> {
        val context = getApplication<Application>()
        val descriptors = SafFileAccess.openReadDescriptors(context, binUris)
        return try {
            val source = descriptors.joinToString(",") { it.fd.toString() }
            FreeArcBackend().list(source)
        } finally {
            descriptors.forEach { it.close() }
        }
    }

    private fun buildTreeScreen(
        installerUri: Uri,
        installerDisplayName: String,
        binNames: List<String>,
        binUris: List<Uri>,
        entries: List<ArchiveEntry>,
    ) {
        val validEntries = entries.mapNotNull { entry ->
            val validated = PathSecurity.validate(entry.path)
            if (validated is PathSecurity.ValidationResult.Valid) entry.copy(path = validated.normalizedPath) else null
        }
        val root = FileTree.build(validEntries)
        val totalBytes = validEntries.sumOf { it.uncompressedSize ?: 0L }

        val messages = buildList {
            add(UserMessage.InnoSetupDetected)
            add(UserMessage.BinFilesFound(binNames.size))
            add(UserMessage.ArcArchiveDetected)
            add(UserMessage.DataSize(humanReadableBytes(totalBytes)))
        }

        _screen.value = Screen.Tree(
            ArchiveAnalysis(
                installerUri = installerUri,
                installerDisplayName = installerDisplayName,
                binFileNames = binNames,
                binFileUris = binUris,
                totalDataBytes = totalBytes,
                root = root,
                selection = emptySet(),
                messages = messages,
            ),
        )
    }

    private inline fun updateTreeAnalysis(transform: (ArchiveAnalysis) -> ArchiveAnalysis) {
        val current = _screen.value as? Screen.Tree ?: return
        _screen.value = current.copy(analysis = transform(current.analysis))
    }

    private fun reportFailure(error: Throwable) {
        _screen.value = when (error) {
            is UnsupportedEntryException -> Screen.Failure(UserMessage.ArchiveCorrupted, error.message)
            is java.io.IOException -> Screen.Failure(UserMessage.PermissionDenied, error.message)
            else -> Screen.Failure(UserMessage.ArchiveCorrupted, error.message)
        }
    }
}

private fun humanReadableBytes(bytes: Long): String {
    val gb = bytes / 1_000_000_000.0
    return if (gb >= 1.0) "%.2f Go".format(gb) else "%.1f Mo".format(bytes / 1_000_000.0)
}
