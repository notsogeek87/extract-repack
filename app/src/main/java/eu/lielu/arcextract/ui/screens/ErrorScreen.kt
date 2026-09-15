package eu.lielu.arcextract.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ErrorOutline
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import eu.lielu.arcextract.core.model.UserMessage

@Composable
fun ErrorScreen(message: UserMessage, detail: String?, onRetry: () -> Unit) {
    Scaffold(topBar = { TopAppBar(title = { Text("ArcExtract") }) }) { padding ->
        Column(
            modifier = Modifier.fillMaxSize().padding(padding).padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(Icons.Filled.ErrorOutline, contentDescription = null, modifier = Modifier.height(48.dp))
            Spacer(Modifier.height(12.dp))
            Text(message.text, style = MaterialTheme.typography.titleMedium)
            if (!detail.isNullOrBlank()) {
                Spacer(Modifier.height(8.dp))
                Text(detail, style = MaterialTheme.typography.bodySmall)
            }
            Spacer(Modifier.height(24.dp))
            Button(onClick = onRetry) { Text("Retour") }
        }
    }
}
