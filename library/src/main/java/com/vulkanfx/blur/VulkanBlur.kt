package com.vulkanfx.blur

import android.graphics.Bitmap
import android.view.Surface

/**
 * Kotlin entry point for the Vulkan Dual Kawase blur engine.
 *
 * Typical SurfaceView host:
 *   attach(surface) → setInputBitmap / setBlurRadius → render() → detach()
 */
class VulkanBlur {
    private var handle: Long = 0
    private var pendingRadius: Float = 24f
    private var pendingBitmap: Bitmap? = null

    @Synchronized
    fun attach(surface: Surface, enableValidation: Boolean = false): String {
        if (handle != 0L) {
            nativeSetSurface(handle, surface)
        } else {
            handle = nativeCreate(surface, enableValidation)
        }
        nativeSetRadius(handle, pendingRadius)
        pendingBitmap?.let {
            nativeSetInputBitmap(handle, it)
            pendingBitmap = null
        }
        return nativeInfo(handle)
    }

    @Synchronized
    fun releaseSurface() {
        if (handle != 0L) nativeReleaseSurface(handle)
    }

    @Synchronized
    fun resize(width: Int, height: Int) {
        if (handle != 0L) nativeResize(handle, width, height)
    }

    @Synchronized
    fun setBlurRadius(radius: Float) {
        pendingRadius = radius.coerceAtLeast(1f)
        if (handle != 0L) nativeSetRadius(handle, pendingRadius)
    }

    /**
     * Pyramid inspect: 0 = final blur, 1 = first downsample, …, N = lowest level.
     */
    @Synchronized
    fun setDebugLevel(level: Int) {
        if (handle != 0L) nativeSetDebugLevel(handle, level.coerceAtLeast(0))
    }

    /** Upload RGBA content to blur. Must be [Bitmap.Config.ARGB_8888], max edge 1280. */
    @Synchronized
    fun setInputBitmap(bitmap: Bitmap) {
        require(bitmap.config == Bitmap.Config.ARGB_8888) { "ARGB_8888 required" }
        if (handle == 0L) {
            pendingBitmap = bitmap
            return
        }
        nativeSetInputBitmap(handle, bitmap)
    }

    /** Re-run Dual Kawase + present. No-op until [attach] has succeeded. */
    @Synchronized
    fun render() {
        if (handle != 0L) nativeRender(handle)
    }

    @Synchronized
    fun info(): String = if (handle != 0L) nativeInfo(handle) else ""

    /** Last measured downsample time in ms, or -1 if unavailable. */
    @get:Synchronized
    val downsampleMs: Float
        get() = if (handle != 0L) nativeDownMs(handle) else -1f

    @get:Synchronized
    val upsampleMs: Float
        get() = if (handle != 0L) nativeUpMs(handle) else -1f

    @get:Synchronized
    val totalMs: Float
        get() = if (handle != 0L) nativeTotalMs(handle) else -1f

    @Synchronized
    fun detach() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
        pendingBitmap = null
    }

    @get:Synchronized
    val isReady: Boolean
        get() = handle != 0L

    private external fun nativeCreate(surface: Surface, enableValidation: Boolean): Long
    private external fun nativeSetSurface(handle: Long, surface: Surface)
    private external fun nativeReleaseSurface(handle: Long)
    private external fun nativeResize(handle: Long, width: Int, height: Int)
    private external fun nativeSetRadius(handle: Long, radius: Float)
    private external fun nativeSetDebugLevel(handle: Long, level: Int)
    private external fun nativeSetInputBitmap(handle: Long, bitmap: Bitmap)
    private external fun nativeRender(handle: Long)
    private external fun nativeInfo(handle: Long): String
    private external fun nativeDownMs(handle: Long): Float
    private external fun nativeUpMs(handle: Long): Float
    private external fun nativeTotalMs(handle: Long): Float
    private external fun nativeDestroy(handle: Long)

    companion object {
        init {
            System.loadLibrary("vulkanblur")
        }
    }
}
