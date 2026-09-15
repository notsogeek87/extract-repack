package eu.lielu.arcextract.backend

import eu.lielu.arcextract.core.backend.ArchiveBackend
import eu.lielu.arcextract.core.backend.ExtractionOutcome
import eu.lielu.arcextract.core.backend.UnsupportedEntryException
import eu.lielu.arcextract.core.detect.InnoSetupDetector
import eu.lielu.arcextract.core.model.ArchiveEntry
import java.io.OutputStream

/**
 * Inno Setup installer backend. `canHandle` genuinely detects the format
 * (see [InnoSetupDetector] — content-based, not extension-based) so the UI
 * can show "Installateur Inno Setup détecté" as soon as a file is picked.
 *
 * `list`/`extract` are **not wired to a real decoder in this build**.
 * innoextract (native/third_party/innoextract, MIT/zlib — see
 * docs/ANALYSIS.md §2) is vendored as a git submodule and a working Android
 * port of it exists upstream (alanwoolley/innoextract-android), but
 * building it here requires cross-compiling Boost + liblzma for Android
 * and an NDK to iterate against, neither of which is available in the
 * sandbox this was developed in. Rather than fake a listing, this throws
 * a clear, specific error — see docs/BUILDING.md for exactly what is left
 * to wire this up for real.
 */
class InnoSetupBackend : ArchiveBackend {
    override val id: String = "inno_setup"

    override fun canHandle(sourcePath: String, headBytes: ByteArray): Boolean =
        InnoSetupDetector.detect(headBytes).isInnoSetup

    override fun list(sourcePath: String): List<ArchiveEntry> {
        throw UnsupportedEntryException(NOT_WIRED_MESSAGE)
    }

    override fun extract(sourcePath: String, entry: ArchiveEntry, destination: OutputStream): ExtractionOutcome {
        throw UnsupportedEntryException(NOT_WIRED_MESSAGE)
    }

    private companion object {
        const val NOT_WIRED_MESSAGE =
            "Le décodage Inno Setup natif (innoextract) n'est pas encore câblé dans cette build. " +
                "Voir docs/BUILDING.md pour l'état exact et les prochaines étapes."
    }
}
