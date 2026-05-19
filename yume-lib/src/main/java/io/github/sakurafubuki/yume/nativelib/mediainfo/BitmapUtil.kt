package io.github.sakurafubuki.yume.nativelib.mediainfo

import android.graphics.Bitmap
import android.graphics.Matrix

internal fun Bitmap.rotateIfNeeded(degrees: Int): Bitmap {
    if (degrees % 360 == 0) return this
    val matrix = Matrix().apply { postRotate(degrees.toFloat()) }
    return Bitmap.createBitmap(this, 0, 0, width, height, matrix, true)
}
