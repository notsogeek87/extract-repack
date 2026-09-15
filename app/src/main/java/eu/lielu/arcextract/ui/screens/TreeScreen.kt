package eu.lielu.arcextract.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.InsertDriveFile
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TriStateCheckbox
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.state.ToggleableState
import androidx.compose.ui.unit.dp
import eu.lielu.arcextract.core.model.UserMessage
import eu.lielu.arcextract.core.tree.FileTree
import eu.lielu.arcextract.core.tree.FileTreeNode
import eu.lielu.arcextract.core.tree.SelectionState
import eu.lielu.arcextract.viewmodel.ArchiveAnalysis

private data class FlatNode(val node: FileTreeNode, val depth: Int)

private fun flatten(node: FileTreeNode, depth: Int = 0): List<FlatNode> {
    if (node.fullPath.isEmpty()) {
        // Root has no visible row of its own; only its children are shown.
        return node.children.values.flatMap { flatten(it, 0) }
    }
    val self = FlatNode(node, depth)
    return listOf(self) + node.children.values.flatMap { flatten(it, depth + 1) }
}

@Composable
fun TreeScreen(
    analysis: ArchiveAnalysis,
    onToggleNode: (FileTreeNode) -> Unit,
    onSelectAll: () -> Unit,
    onDeselectAll: () -> Unit,
    onSearchQueryChange: (String) -> Unit,
    onExtractClick: () -> Unit,
    onBack: () -> Unit,
) {
    val visibleNodes = if (analysis.searchQuery.isBlank()) {
        flatten(analysis.root)
    } else {
        FileTree.search(analysis.root, analysis.searchQuery).map { FlatNode(it, 0) }
    }
    val selectedBytes = FileTree.totalSelectedSize(analysis.root, analysis.selection)

    Scaffold(
        topBar = { TopAppBar(title = { Text(analysis.installerDisplayName) }) },
        bottomBar = {
            Row(
                modifier = Modifier.fillMaxWidth().padding(12.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                OutlinedButton(onClick = onSelectAll) { Text("Tout sélectionner") }
                OutlinedButton(onClick = onDeselectAll) { Text("Tout désélectionner") }
                Button(onClick = onExtractClick, enabled = analysis.selection.isNotEmpty()) { Text("Extraire") }
            }
        },
    ) { padding ->
        Column(modifier = Modifier.fillMaxSize().padding(padding).padding(horizontal = 12.dp)) {
            Spacer(Modifier.height(8.dp))
            for (message in analysis.messages) {
                Text(messageText(message), style = MaterialTheme.typography.bodyMedium)
            }
            Text(
                "${analysis.selection.size} fichier(s) sélectionné(s) — ${humanReadableBytes(selectedBytes)}",
                style = MaterialTheme.typography.labelLarge,
            )
            Spacer(Modifier.height(8.dp))

            OutlinedTextField(
                value = analysis.searchQuery,
                onValueChange = onSearchQueryChange,
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
                placeholder = { Text("Rechercher un fichier ou un dossier…") },
            )
            Spacer(Modifier.height(8.dp))

            LazyColumn(modifier = Modifier.fillMaxSize()) {
                items(visibleNodes, key = { it.node.fullPath }) { flat ->
                    TreeRow(
                        flat = flat,
                        state = FileTree.selectionState(flat.node, analysis.selection),
                        onToggle = { onToggleNode(flat.node) },
                    )
                }
            }
        }
    }
}

@Composable
private fun TreeRow(flat: FlatNode, state: SelectionState, onToggle: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(start = (flat.depth * 20).dp, top = 2.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TriStateCheckbox(
            state = when (state) {
                SelectionState.ALL -> ToggleableState.On
                SelectionState.PARTIAL -> ToggleableState.Indeterminate
                SelectionState.NONE -> ToggleableState.Off
            },
            onClick = onToggle,
        )
        Icon(
            if (flat.node.isDirectory) Icons.Filled.Folder else Icons.Filled.InsertDriveFile,
            contentDescription = null,
        )
        Spacer(Modifier.width(6.dp))
        Column(modifier = Modifier.fillMaxWidth()) {
            Text(flat.node.name.ifEmpty { flat.node.fullPath }, style = MaterialTheme.typography.bodyLarge)
            if (!flat.node.isDirectory) {
                Text(humanReadableBytes(flat.node.totalUncompressedSize()), style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

private fun messageText(message: UserMessage): String = message.text

private fun humanReadableBytes(bytes: Long): String {
    val gb = bytes / 1_000_000_000.0
    if (gb >= 1.0) return "%.2f Go".format(gb)
    val mb = bytes / 1_000_000.0
    if (mb >= 1.0) return "%.1f Mo".format(mb)
    return "$bytes o"
}
