package io.github.sakurafubuki.yume.nativelib.renderer

import androidx.media3.common.C
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer

/**
 * Abstract base class for renderers that support time synchronization adjustments.
 *
 * Provides offset (delay) and speed multiplier adjustments for synchronizing
 * subtitles/audio tracks with video playback.
 */
open class OffsetRenderer {

    var syncOffsetMilliseconds: Long = 0L
        set(value) {
            field = value
            syncOffsetUs = value * 1000
        }

    var syncSpeedMultiplier: Float = 1.0f
        set(value) {
            field = value.coerceIn(0.1f, 10f)
        }

    private var syncOffsetUs: Long = 0L

    fun getOffsetAdjustedPositionUs(positionUs: Long): Long {
        return (positionUs * syncSpeedMultiplier).toLong() - syncOffsetUs
    }
}


/**
 * Helper function to retrieve an [OffsetRenderer] for a specific track type.
 */
@UnstableApi
internal fun ExoPlayer.getOffsetRenderer(trackType: @C.TrackType Int): OffsetRenderer? {
    for (i in 0 until rendererCount) {
        if (getRendererType(i) == trackType) {
            return getRenderer(i) as? OffsetRenderer
        }
    }
    return null
}
