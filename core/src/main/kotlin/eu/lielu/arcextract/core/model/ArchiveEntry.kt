package eu.lielu.arcextract.core.model

/**
 * One entry discovered while listing an installer/archive, before any
 * extraction happens. [path] is always a normalized, forward-slash,
 * relative path (see [eu.lielu.arcextract.core.security.PathSecurity]) —
 * never an absolute path and never containing ".." segments.
 */
data class ArchiveEntry(
    val path: String,
    val type: EntryType,
    val compressedSize: Long?,
    val uncompressedSize: Long?,
    val compressionMethod: CompressionMethodInfo,
    val sourceArchive: String,
    val crc32: Long? = null,
    /**
     * Opaque, backend-defined data needed to re-locate this entry for
     * extraction without re-parsing the whole container (e.g. the
     * FreeArc backend encodes its solid-block absolute file offset here).
     * Never interpreted outside the [eu.lielu.arcextract.core.backend.ArchiveBackend]
     * that produced it.
     */
    val backendData: String? = null,
) {
    val name: String get() = path.substringAfterLast('/')

    val isExtractable: Boolean get() = compressionMethod.status == SupportStatus.EXTRACTABLE
}
