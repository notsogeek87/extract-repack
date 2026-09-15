package eu.lielu.arcextract.core.model

/**
 * Whether ArcExtract can actually produce bytes for an entry compressed
 * with a given method, *in this build*. This must only ever be widened
 * (UNSUPPORTED/PLANNED -> EXTRACTABLE) once a real, tested code path
 * exists — never to make the UI look more capable than it is.
 */
enum class SupportStatus {
    /** A real decoder is wired in this build and has been exercised by a test. */
    EXTRACTABLE,

    /** Backend source is vendored/architected but decoding is not wired yet. */
    PLANNED,

    /** Known to be out of scope (licensing, portability, or complexity). */
    UNSUPPORTED,
}

data class CompressionMethodInfo(
    val id: String,
    val label: String,
    val status: SupportStatus,
    val reason: String,
)

/**
 * Registry of compression methods ArcExtract knows how to *recognize*.
 * See docs/ANALYSIS.md §5 for the capability matrix this mirrors.
 */
object CompressionMethods {
    val STORE = CompressionMethodInfo(
        id = "store",
        label = "Stocké (non compressé)",
        status = SupportStatus.EXTRACTABLE,
        reason = "",
    )
    val LZMA = CompressionMethodInfo(
        id = "lzma",
        label = "LZMA",
        status = SupportStatus.PLANNED,
        reason = "Backend FreeArc vendorisé (native/third_party/freearc) mais décodage non câblé dans cette version.",
    )
    val LZMA2 = LZMA.copy(id = "lzma2", label = "LZMA2")
    val PPMD = CompressionMethodInfo(
        id = "ppmd",
        label = "PPMd",
        status = SupportStatus.PLANNED,
        reason = "Backend FreeArc vendorisé mais décodage non câblé dans cette version.",
    )
    val REP = CompressionMethodInfo(
        id = "rep",
        label = "REP",
        status = SupportStatus.PLANNED,
        reason = "Backend FreeArc vendorisé mais décodage non câblé dans cette version.",
    )
    val SREP = CompressionMethodInfo(
        id = "srep",
        label = "SREP",
        status = SupportStatus.UNSUPPORTED,
        reason = "Cette archive utilise SREP : support Android indisponible (dictionnaire trop volumineux pour un usage mobile fiable).",
    )
    val XTOOL = CompressionMethodInfo(
        id = "xtool",
        label = "XTool",
        status = SupportStatus.UNSUPPORTED,
        reason = "Cette archive utilise XTool (Reflate/Precomp) : support Android indisponible.",
    )
    val GRZIP = CompressionMethodInfo(
        id = "grzip",
        label = "GRZip",
        status = SupportStatus.UNSUPPORTED,
        reason = "Méthode de compression non supportée (GRZip).",
    )
    val TORNADO = CompressionMethodInfo(
        id = "tornado",
        label = "Tornado",
        status = SupportStatus.UNSUPPORTED,
        reason = "Méthode de compression non supportée (Tornado).",
    )

    private val known = listOf(STORE, LZMA, LZMA2, PPMD, REP, SREP, XTOOL, GRZIP, TORNADO)
        .associateBy { it.id }

    fun fromId(rawId: String): CompressionMethodInfo {
        val normalized = rawId.trim().lowercase()
        val alias = when (normalized) {
            "stored", "raw" -> "store"
            else -> normalized
        }
        return known[alias] ?: CompressionMethodInfo(
            id = normalized,
            label = rawId,
            status = SupportStatus.UNSUPPORTED,
            reason = "Méthode de compression non supportée ($rawId).",
        )
    }
}
