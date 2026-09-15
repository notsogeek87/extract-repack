package eu.lielu.arcextract.core.tree

import com.google.common.truth.Truth.assertThat
import eu.lielu.arcextract.core.model.ArchiveEntry
import eu.lielu.arcextract.core.model.CompressionMethods
import eu.lielu.arcextract.core.model.EntryType
import org.junit.jupiter.api.Test

class FileTreeTest {

    private fun file(path: String, size: Long) = ArchiveEntry(
        path = path,
        type = EntryType.FILE,
        compressedSize = size / 2,
        uncompressedSize = size,
        compressionMethod = CompressionMethods.STORE,
        sourceArchive = "fg-01.bin",
    )

    private val sampleEntries = listOf(
        file("app/data/bis/a.dat", 100),
        file("app/data/dlc/b.dat", 200),
        file("app/data/games/game.bin", 5_000),
        file("app/user/settings.ini", 10),
    )

    @Test
    fun `builds a tree matching the spec's example layout`() {
        val root = FileTree.build(sampleEntries)
        val app = root.children.getValue("app")
        assertThat(app.isDirectory).isTrue()
        assertThat(app.children.keys).containsExactly("data", "user")
        val data = app.children.getValue("data")
        assertThat(data.children.keys).containsExactly("bis", "dlc", "games")
    }

    @Test
    fun `recursive select-all selects every file`() {
        val root = FileTree.build(sampleEntries)
        val selection = FileTree.selectAll(root)
        assertThat(selection).containsExactly(
            "app/data/bis/a.dat",
            "app/data/dlc/b.dat",
            "app/data/games/game.bin",
            "app/user/settings.ini",
        )
    }

    @Test
    fun `selecting a folder selects only its descendants, not siblings`() {
        val root = FileTree.build(sampleEntries)
        val gamesNode = root.find("app/data/games")!!
        val selection = FileTree.setSelected(emptySet(), gamesNode, selected = true)
        assertThat(selection).containsExactly("app/data/games/game.bin")
    }

    @Test
    fun `partial selection state bubbles up correctly`() {
        val root = FileTree.build(sampleEntries)
        val dataNode = root.find("app/data")!!
        val bisNode = root.find("app/data/bis")!!
        var selection = emptySet<String>()
        selection = FileTree.setSelected(selection, bisNode, selected = true)

        assertThat(FileTree.selectionState(bisNode, selection)).isEqualTo(SelectionState.ALL)
        assertThat(FileTree.selectionState(dataNode, selection)).isEqualTo(SelectionState.PARTIAL)
    }

    @Test
    fun `toggle on a fully selected node deselects it`() {
        val root = FileTree.build(sampleEntries)
        val bisNode = root.find("app/data/bis")!!
        var selection = FileTree.setSelected(emptySet(), bisNode, selected = true)
        selection = FileTree.toggle(selection, bisNode)
        assertThat(FileTree.selectionState(bisNode, selection)).isEqualTo(SelectionState.NONE)
    }

    @Test
    fun `total size excludes app_user and tmp when only app_data_games is selected`() {
        val root = FileTree.build(sampleEntries)
        val gamesNode = root.find("app/data/games")!!
        val selection = FileTree.setSelected(emptySet(), gamesNode, selected = true)
        assertThat(FileTree.totalSelectedSize(root, selection)).isEqualTo(5_000L)
    }

    @Test
    fun `search finds nodes by substring anywhere in the path`() {
        val root = FileTree.build(sampleEntries)
        val results = FileTree.search(root, "game")
        assertThat(results.map { it.fullPath }).contains("app/data/games/game.bin")
    }
}
