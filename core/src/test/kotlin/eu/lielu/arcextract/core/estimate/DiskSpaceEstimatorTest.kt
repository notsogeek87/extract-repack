package eu.lielu.arcextract.core.estimate

import com.google.common.truth.Truth.assertThat
import org.junit.jupiter.api.Test

class DiskSpaceEstimatorTest {

    @Test
    fun `reports sufficient when available exceeds required plus margin`() {
        val estimate = DiskSpaceEstimator.estimate(
            totalUncompressedBytes = 10_000_000_000L,
            availableBytes = 20_000_000_000L,
        )
        assertThat(estimate.sufficient).isTrue()
        assertThat(estimate.requiredBytes).isGreaterThan(10_000_000_000L)
    }

    @Test
    fun `reports insufficient when available is below required`() {
        val estimate = DiskSpaceEstimator.estimate(
            totalUncompressedBytes = 19_000_000_000L,
            availableBytes = 18_000_000_000L,
        )
        assertThat(estimate.sufficient).isFalse()
    }

    @Test
    fun `applies the safety margin on top of the raw byte sum`() {
        val estimate = DiskSpaceEstimator.estimate(
            totalUncompressedBytes = 1_000_000L,
            availableBytes = 1_010_000L,
            safetyMarginRatio = 0.02,
        )
        assertThat(estimate.marginBytes).isEqualTo(20_000L)
        assertThat(estimate.requiredBytes).isEqualTo(1_020_000L)
        assertThat(estimate.sufficient).isFalse()
    }

    @Test
    fun `zero bytes selected is always sufficient`() {
        val estimate = DiskSpaceEstimator.estimate(totalUncompressedBytes = 0, availableBytes = 0)
        assertThat(estimate.sufficient).isTrue()
    }
}
