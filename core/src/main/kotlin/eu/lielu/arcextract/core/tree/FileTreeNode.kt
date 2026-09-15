package eu.lielu.arcextract.core.tree

import eu.lielu.arcextract.core.model.ArchiveEntry

/**
 * Immutable tree built from a flat [ArchiveEntry] listing. Directory nodes
 * are synthesized from path components even if the source archive never
 * emits an explicit directory entry for them (FreeArc/Inno Setup listings
 * are not guaranteed to).
 */
data class FileTreeNode(
    val name: String,
    val fullPath: String,
    val isDirectory: Boolean,
    val entry: ArchiveEntry?,
    val children: Map<String, FileTreeNode>,
) {
    /** All file (leaf) paths under this node, this node included if it is a file. */
    fun allFilePaths(): List<String> {
        if (!isDirectory) return listOf(fullPath)
        return children.values.flatMap { it.allFilePaths() }
    }

    /** Sum of [ArchiveEntry.uncompressedSize] for every file under this node; null components are treated as 0. */
    fun totalUncompressedSize(): Long {
        if (!isDirectory) return entry?.uncompressedSize ?: 0L
        return children.values.sumOf { it.totalUncompressedSize() }
    }

    fun fileCount(): Int {
        if (!isDirectory) return 1
        return children.values.sumOf { it.fileCount() }
    }

    /** Depth-first, directories before their children, useful for flattened UI lists. */
    fun walk(): Sequence<FileTreeNode> = sequence {
        yield(this@FileTreeNode)
        for (child in children.values) {
            yieldAll(child.walk())
        }
    }

    fun find(path: String): FileTreeNode? {
        if (path == fullPath) return this
        if (!path.startsWith("$fullPath/") && fullPath.isNotEmpty()) return null
        val remainder = if (fullPath.isEmpty()) path else path.removePrefix("$fullPath/")
        val nextSegment = remainder.substringBefore('/')
        return children[nextSegment]?.find(path)
    }
}
