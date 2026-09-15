@file:OptIn(ExperimentalMaterial3Api::class)

package eu.lielu.arcextract.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import eu.lielu.arcextract.viewmodel.ExtractionSummary

@Composable
fun ResultScreen(summary: ExtractionSummary, onDone: () -> Unit) {
    Scaffold(topBar = { TopAppBar(title = { Text("Extraction terminée") }) }) { padding ->
        Column(
            modifier = Modifier.fillMaxSize().padding(padding).padding(24.dp),
            verticalArrangement = Arrangement.Top,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("Extraction terminée", style = MaterialTheme.typography.headlineSmall)
            Spacer(Modifier.height(16.dp))
            Text("${summary.fileCount} fichiers")
            Text(humanReadableBytes(summary.totalBytes))
            Text("Durée : ${durationText(summary.durationMillis)}")

            if (summary.skipped.isNotEmpty()) {
                Spacer(Modifier.height(16.dp))
                Text(
                    "${summary.skipped.size} fichier(s) ignoré(s) :",
                    style = MaterialTheme.typography.titleSmall,
                )
                LazyColumn(modifier = Modifier.fillMaxWidth().height(160.dp)) {
                    items(summary.skipped) { (path, reason) ->
                        Text("• $path — $reason", style = MaterialTheme.typography.bodySmall)
                    }
                }
            }

            Spacer(Modifier.height(24.dp))
            Button(onClick = onDone) { Text("Terminer") }
        }
    }
}

private fun durationText(durationMillis: Long): String {
    val totalSeconds = durationMillis / 1000
    val minutes = totalSeconds / 60
    val seconds = totalSeconds % 60
    return if (minutes > 0) "${minutes} min ${seconds} s" else "${seconds} s"
}

private fun humanReadableBytes(bytes: Long): String {
    val gb = bytes / 1_000_000_000.0
    if (gb >= 1.0) return "%.2f Go".format(gb)
    val mb = bytes / 1_000_000.0
    if (mb >= 1.0) return "%.1f Mo".format(mb)
    return "$bytes o"
}
