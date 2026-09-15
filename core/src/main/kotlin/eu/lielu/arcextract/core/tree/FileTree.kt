package eu.lielu.arcextract.core.tree

import eu.lielu.arcextract.core.model.ArchiveEntry

enum class SelectionState { NONE, PARTIAL, ALL }

object FileTree {

    /**
     * Builds a tree from a flat entry list. Entries with paths rejected by
     * [eu.lielu.arcextract.core.security.PathSecurity] must be filtered out
     * by the caller *before* calling this — this function trusts that every
     * [ArchiveEntry.path] it receives is already validated and normalized.
     */
    fun build(entries: List<ArchiveEntry>): FileTreeNode {
        val root = MutableNode("", "")
        for (entry in entries) {
            val segments = entry.path.split('/')
            var current = root
            for ((index, segment) in segments.withIndex()) {
                val isLast = index == segments.lastIndex
                val childPath = if (current.fullPath.isEmpty()) segment else "${current.fullPath}/$segment"
                current = current.children.getOrPut(segment) {
                    MutableNode(segment, childPath)
                }
                if (isLast) {
                    current.isDirectory = entry.type == eu.lielu.arcextract.core.model.EntryType.DIRECTORY
                    current.entry = entry
                } else {
                    current.isDirectory = true
                }
            }
        }
        return root.freeze()
    }

    fun selectAll(root: FileTreeNode): Set<String> = root.allFilePaths().toSet()

    fun deselectAll(): Set<String> = emptySet()

    /** Returns a new selection set with every file under [node] added (selected) or removed (deselected). */
    fun setSelected(currentSelection: Set<String>, node: FileTreeNode, selected: Boolean): Set<String> {
        val affected = node.allFilePaths()
        return if (selected) currentSelection + affected else currentSelection - affected.toSet()
    }

    /** Toggles a node: if it is currently fully or partially selected, deselect it entirely; otherwise select it entirely. */
    fun toggle(currentSelection: Set<String>, node: FileTreeNode): Set<String> {
        val state = selectionState(node, currentSelection)
        val nowSelect = state == SelectionState.NONE
        return setSelected(currentSelection, node, nowSelect)
    }

    fun selectionState(node: FileTreeNode, selection: Set<String>): SelectionState {
        val files = node.allFilePaths()
        if (files.isEmpty()) return SelectionState.NONE
        val selectedCount = files.count { it in selection }
        return when {
            selectedCount == 0 -> SelectionState.NONE
            selectedCount == files.size -> SelectionState.ALL
            else -> SelectionState.PARTIAL
        }
    }

    fun totalSelectedSize(root: FileTreeNode, selection: Set<String>): Long {
        return selection.sumOf { path -> root.find(path)?.entry?.uncompressedSize ?: 0L }
    }

    fun search(root: FileTreeNode, query: String): List<FileTreeNode> {
        if (query.isBlank()) return emptyList()
        val needle = query.trim().lowercase()
        return root.walk()
            .filter { it.fullPath.isNotEmpty() && it.fullPath.lowercase().contains(needle) }
            .toList()
    }

    private class MutableNode(val name: String, val fullPath: String) {
        var isDirectory: Boolean = false
        var entry: ArchiveEntry? = null
        val children: LinkedHashMap<String, MutableNode> = LinkedHashMap()

        fun freeze(): FileTreeNode = FileTreeNode(
            name = name,
            fullPath = fullPath,
            isDirectory = isDirectory || children.isNotEmpty(),
            entry = entry,
            children = children.mapValues { it.value.freeze() },
        )
    }
}
