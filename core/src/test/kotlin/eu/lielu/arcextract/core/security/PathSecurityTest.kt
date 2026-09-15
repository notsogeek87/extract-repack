package eu.lielu.arcextract.core.security

import com.google.common.truth.Truth.assertThat
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

class PathSecurityTest {

    @Test
    fun `accepts a normal nested path`() {
        val result = PathSecurity.validate("app/data/games/file.dat")
        assertThat(result).isInstanceOf(PathSecurity.ValidationResult.Valid::class.java)
        assertThat((result as PathSecurity.ValidationResult.Valid).normalizedPath)
            .isEqualTo("app/data/games/file.dat")
    }

    @Test
    fun `normalizes windows-style backslashes`() {
        val result = PathSecurity.validate("app\\data\\games\\file.dat")
        assertThat(result).isInstanceOf(PathSecurity.ValidationResult.Valid::class.java)
        assertThat((result as PathSecurity.ValidationResult.Valid).normalizedPath)
            .isEqualTo("app/data/games/file.dat")
    }

    @Test
    fun `rejects parent directory traversal`() {
        val result = PathSecurity.validate("../../etc/passwd")
        assertThat(result).isEqualTo(PathSecurity.ValidationResult.Rejected(PathSecurity.RejectReason.PARENT_TRAVERSAL))
    }

    @Test
    fun `rejects traversal hidden in the middle of a path`() {
        val result = PathSecurity.validate("app/../../../etc/passwd")
        assertThat(result).isEqualTo(PathSecurity.ValidationResult.Rejected(PathSecurity.RejectReason.PARENT_TRAVERSAL))
    }

    @Test
    fun `rejects absolute unix paths`() {
        val result = PathSecurity.validate("/etc/passwd")
        assertThat(result).isEqualTo(PathSecurity.ValidationResult.Rejected(PathSecurity.RejectReason.ABSOLUTE_PATH))
    }

    @Test
    fun `rejects windows drive letters`() {
        val result = PathSecurity.validate("C:/Windows/System32")
        assertThat(result).isEqualTo(PathSecurity.ValidationResult.Rejected(PathSecurity.RejectReason.WINDOWS_DRIVE_LETTER))
    }

    @Test
    fun `rejects a NUL byte`() {
        val result = PathSecurity.validate("app/data\u0000/evil")
        assertThat(result).isEqualTo(PathSecurity.ValidationResult.Rejected(PathSecurity.RejectReason.NUL_BYTE))
    }

    @Test
    fun `rejects windows reserved device names`() {
        val result = PathSecurity.validate("app/CON.txt")
        assertThat(result).isEqualTo(PathSecurity.ValidationResult.Rejected(PathSecurity.RejectReason.RESERVED_COMPONENT))
    }

    @Test
    fun `rejects an overly long path component`() {
        val longName = "a".repeat(300)
        val result = PathSecurity.validate("app/$longName")
        assertThat(result).isEqualTo(PathSecurity.ValidationResult.Rejected(PathSecurity.RejectReason.COMPONENT_TOO_LONG))
    }

    @Test
    fun `rejects an empty path`() {
        val result = PathSecurity.validate("")
        assertThat(result).isEqualTo(PathSecurity.ValidationResult.Rejected(PathSecurity.RejectReason.EMPTY))
    }

    @Test
    fun `resolveWithinRoot resolves a valid relative path`() {
        val resolved = PathSecurity.resolveWithinRoot("/storage/emulated/0/ArcExtract", "app/data/games/file.dat")
        assertThat(resolved).isEqualTo("/storage/emulated/0/ArcExtract/app/data/games/file.dat")
    }

    @Test
    fun `resolveWithinRoot throws if a crafted path would still escape the root`() {
        assertThrows<PathSecurity.PathTraversalException> {
            // Even a pre-"validated" string containing an encoded escape must still be caught here
            // as defense in depth — this simulates a bug upstream that let ".." slip through.
            PathSecurity.resolveWithinRoot("/root/target", "../outside/file.dat")
        }
    }
}
