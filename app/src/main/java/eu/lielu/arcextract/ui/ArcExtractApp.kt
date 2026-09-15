package eu.lielu.arcextract.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.viewmodel.compose.viewModel
import eu.lielu.arcextract.ui.screens.ErrorScreen
import eu.lielu.arcextract.ui.screens.PickerScreen
import eu.lielu.arcextract.ui.screens.ProgressScreen
import eu.lielu.arcextract.ui.screens.ResultScreen
import eu.lielu.arcextract.ui.screens.TreeScreen
import eu.lielu.arcextract.viewmodel.ArcExtractViewModel
import eu.lielu.arcextract.viewmodel.ExtractionProgress
import eu.lielu.arcextract.viewmodel.Screen

@Composable
fun ArcExtractApp(viewModel: ArcExtractViewModel = viewModel()) {
    val context = LocalContext.current
    val screen by viewModel.screen.collectAsState()

    val pickSetupExeLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { viewModel.onSetupExePicked(it) }
    }
    val pickFolderLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            context.contentResolver.takePersistableUriPermission(
                uri,
                android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION,
            )
            viewModel.onFolderPicked(uri)
        }
    }
    val pickDestinationLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            context.contentResolver.takePersistableUriPermission(
                uri,
                android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
            )
            viewModel.startExtraction(uri)
        }
    }

    when (val current = screen) {
        is Screen.Picker -> PickerScreen(
            onPickSetupExe = { pickSetupExeLauncher.launch(arrayOf("application/octet-stream", "application/x-msdownload", "*/*")) },
            onPickFolder = { pickFolderLauncher.launch(null) },
        )

        is Screen.Analyzing -> ProgressScreen(
            progress = ExtractionProgress(
                currentFileName = "Analyse en cours…",
                filesDone = 0,
                filesTotal = 0,
                bytesDone = 0,
                bytesTotal = 0,
                bytesPerSecond = 0.0,
                etaSeconds = null,
            ),
            onCancel = { viewModel.reset() },
        )

        is Screen.Tree -> TreeScreen(
            analysis = current.analysis,
            onToggleNode = viewModel::toggleNode,
            onSelectAll = viewModel::selectAll,
            onDeselectAll = viewModel::deselectAll,
            onSearchQueryChange = viewModel::updateSearchQuery,
            onExtractClick = { pickDestinationLauncher.launch(null) },
            onBack = { viewModel.reset() },
        )

        is Screen.Extracting -> ProgressScreen(
            progress = current.progress,
            onCancel = { viewModel.cancelExtraction() },
        )

        is Screen.Done -> ResultScreen(
            summary = current.summary,
            onDone = { viewModel.reset() },
        )

        is Screen.Failure -> ErrorScreen(
            message = current.message,
            detail = current.detail,
            onRetry = { viewModel.reset() },
        )
    }
}
