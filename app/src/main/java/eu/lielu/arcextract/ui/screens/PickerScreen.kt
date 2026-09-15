@file:OptIn(ExperimentalMaterial3Api::class)

package eu.lielu.arcextract.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.InsertDriveFile
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun PickerScreen(
    onPickSetupExe: () -> Unit,
    onPickFolder: () -> Unit,
) {
    Scaffold(topBar = { TopAppBar(title = { Text("ArcExtract") }) }) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("Choisir un installateur", style = MaterialTheme.typography.headlineSmall)
            Spacer(Modifier.height(12.dp))
            Text(
                "Sélectionnez un installateur Inno Setup (setup.exe) ou le dossier qui le contient avec ses fichiers .bin.",
                style = MaterialTheme.typography.bodyMedium,
            )
            Spacer(Modifier.height(24.dp))

            Button(onClick = onPickSetupExe, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Filled.InsertDriveFile, contentDescription = null)
                Spacer(Modifier.height(0.dp))
                Text("  Choisir setup.exe")
            }
            Spacer(Modifier.height(8.dp))
            OutlinedButton(onClick = onPickFolder, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Filled.Folder, contentDescription = null)
                Text("  Choisir un dossier")
            }
            Spacer(Modifier.height(16.dp))
            Text(
                "Astuce : \"Choisir un dossier\" est recommandé — Android ne garantit pas toujours " +
                    "l'accès aux fichiers .bin voisins lorsqu'on choisit uniquement setup.exe.",
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}
