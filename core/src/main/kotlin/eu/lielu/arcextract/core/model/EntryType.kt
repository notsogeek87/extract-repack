package eu.lielu.arcextract.core.model

/**
 * Only regular files and directories are ever materialized on disk.
 * There is intentionally no SYMLINK case: even if a parsed archive entry
 * claims to be a symlink, extraction code must never honor that — it is
 * treated as a plain file. This is enforced by the type system rather than
 * by a runtime check.
 */
enum class EntryType {
    FILE,
    DIRECTORY,
}
