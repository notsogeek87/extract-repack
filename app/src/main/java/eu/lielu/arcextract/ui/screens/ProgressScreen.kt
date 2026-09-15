@file:OptIn(ExperimentalMaterial3Api::class)

package eu.lielu.arcextract.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import eu.lielu.arcextract.viewmodel.ExtractionProgress

@Composable
fun ProgressScreen(progress: ExtractionProgress, onCancel: () -> Unit) {
    val fraction = if (progress.bytesTotal > 0) (progress.bytesDone.toFloat() / progress.bytesTotal).coerceIn(0f, 1f) else 0f
    val percent = (fraction * 100).toInt()

    Scaffold(topBar = { TopAppBar(title = { Text("Extraction en cours") }) }) { padding ->
        Column(
            modifier = Modifier.fillMaxSize().padding(padding).padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(progress.currentFileName, style = MaterialTheme.typography.bodyMedium)
            Spacer(Modifier.height(12.dp))
            LinearProgressIndicator(progress = { fraction }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            Text("$percent %", style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(16.dp))
            Text(
                "${humanReadableBytes(progress.bytesDone)} / ${humanReadableBytes(progress.bytesTotal)}  " +
                    "(${progress.filesDone}/${progress.filesTotal} fichiers)",
            )
            Text("${humanReadableSpeed(progress.bytesPerSecond)}")
            Text(etaText(progress.etaSeconds))
            Spacer(Modifier.height(24.dp))
            OutlinedButton(onClick = onCancel) { Text("Annuler") }
        }
    }
}

private fun etaText(etaSeconds: Long?): String {
    if (etaSeconds == null) return "Temps restant : calcul en cours…"
    val minutes = etaSeconds / 60
    val seconds = etaSeconds % 60
    return if (minutes > 0) "~${minutes} min ${seconds} s restantes" else "~${seconds} secondes restantes"
}

private fun humanReadableSpeed(bytesPerSecond: Double): String {
    val mbps = bytesPerSecond / 1_000_000.0
    return "%.1f Mo/s".format(mbps)
}

private fun humanReadableBytes(bytes: Long): String {
    val gb = bytes / 1_000_000_000.0
    if (gb >= 1.0) return "%.2f Go".format(gb)
    val mb = bytes / 1_000_000.0
    if (mb >= 1.0) return "%.1f Mo".format(mb)
    return "$bytes o"
}
