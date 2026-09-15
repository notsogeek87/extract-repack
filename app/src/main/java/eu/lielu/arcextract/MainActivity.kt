package eu.lielu.arcextract

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import eu.lielu.arcextract.ui.ArcExtractApp
import eu.lielu.arcextract.ui.theme.ArcExtractTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            ArcExtractTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    ArcExtractApp()
                }
            }
        }
    }
}
