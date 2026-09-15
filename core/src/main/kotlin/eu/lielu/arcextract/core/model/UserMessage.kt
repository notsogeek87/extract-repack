package eu.lielu.arcextract.core.model

/**
 * The fixed vocabulary of user-facing status/error messages the app can
 * show, centralized so the same wording is used everywhere (UI, logs,
 * tests) instead of ad hoc strings scattered across call sites. French
 * text mirrors the wording requested in the product spec verbatim.
 */
sealed class UserMessage(val text: String) {
    data object InnoSetupDetected : UserMessage("Installateur Inno Setup détecté")
    data class BinFilesFound(val count: Int) : UserMessage("$count fichiers .bin trouvés")
    data object BinFilesMissing : UserMessage("Archives .bin manquantes")
    data object ArcArchiveDetected : UserMessage("Archive ARC détectée")
    data class UnsupportedMethod(val methodLabel: String, val reason: String) :
        UserMessage("Méthode de compression non supportée : $methodLabel — $reason")
    data object InsufficientDiskSpace : UserMessage("Espace disque insuffisant")
    data object PermissionDenied : UserMessage("Permission d'accès refusée")
    data object ArchiveCorrupted : UserMessage("Archive corrompue")
    data object NotInnoSetup : UserMessage("Ce fichier n'est pas un installateur Inno Setup reconnu")
    data class DataSize(val humanReadable: String) : UserMessage("Taille des données : $humanReadable")
}
