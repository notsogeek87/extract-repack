package eu.lielu.arcextract.core.detect

/**
 * Given the file names present in the same folder as a selected
 * `setup.exe`, finds the external `.bin` payload files Inno Setup style
 * installers split their data into (e.g. `fg-01.bin`, `fg-02.bin`,
 * `fg-03.bin`). This is a folder-listing heuristic only — it does not
 * open any file. The authoritative list of files an installer actually
 * expects comes from parsing `setup.exe` itself (native `InnoSetupBackend`);
 * this finder is what lets the UI show "3 fichiers .bin trouvés" right
 * after folder selection, before that parse has happened.
 */
object SiblingBinFinder {

    /** Natural sort so "fg-2.bin" sorts before "fg-10.bin". */
    private val NATURAL_CHUNK = Regex("(\\d+)|(\\D+)")

    private val naturalOrder = Comparator<String> { a, b ->
        val chunksA = NATURAL_CHUNK.findAll(a).map { it.value }.toList()
        val chunksB = NATURAL_CHUNK.findAll(b).map { it.value }.toList()
        val len = minOf(chunksA.size, chunksB.size)
        for (i in 0 until len) {
            val ca = chunksA[i]
            val cb = chunksB[i]
            val na = ca.toLongOrNull()
            val nb = cb.toLongOrNull()
            val cmp = if (na != null && nb != null) na.compareTo(nb) else ca.compareTo(cb)
            if (cmp != 0) return@Comparator cmp
        }
        chunksA.size.compareTo(chunksB.size)
    }

    fun findBinFiles(namesInFolder: List<String>): List<String> {
        return namesInFolder
            .filter { it.endsWith(".bin", ignoreCase = true) }
            .sortedWith(naturalOrder)
    }
}
