package com.vulkanfx.blur

import android.graphics.Bitmap
import android.view.Surface

/**
 * Kotlin entry point for the Vulkan Dual Kawase blur engine.
 *
 * Call [setInputBitmap] before [render]. Typical SurfaceView flow:
 *   attach(surface) → setInputBitmap → render() each frame → detach()
 */
class VulkanBlur {
    private var handle: Long = 0
    private var pendingRadius: Float = 24f
    private var pendingLayerAlpha: Float = 1f
    private var pendingBlurAlpha: Float = 1f
    private var pendingBlurScale: Float = 1f
    private var pendingRegions: Array<BlurRegion> = emptyArray()
    private var pendingBlurRegionTransform: FloatArray = BLUR_REGION_TRANSFORM_IDENTITY.copyOf()
    private var pendingGlassRimEnabled = false
    private var pendingGlassRimNightMode = false
    private var pendingBitmap: Bitmap? = null
    private var inputReady = false

    @Synchronized
    fun attach(surface: Surface, enableValidation: Boolean = false): String {
        if (handle != 0L) {
            nativeSetSurface(handle, surface)
        } else {
            handle = nativeCreate(surface, enableValidation)
        }
        nativeSetRadius(handle, pendingRadius)
        nativeSetLayerAlpha(handle, pendingLayerAlpha)
        nativeSetBlurAlpha(handle, pendingBlurAlpha)
        nativeSetBlurScale(handle, pendingBlurScale)
        if (pendingRegions.isNotEmpty()) {
            nativeSetBlurRegions(handle, pendingRegions)
        }
        nativeSetBlurRegionTransform(handle, pendingBlurRegionTransform)
        nativeSetGlassRimEnabled(handle, pendingGlassRimEnabled)
        nativeSetGlassRimNightMode(handle, pendingGlassRimNightMode)
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

    /** AOSP LayerSnapshotBuilder: effective radius = requestedRadius * layerAlpha. */
    @Synchronized
    fun setLayerAlpha(alpha: Float) {
        pendingLayerAlpha = alpha.coerceIn(0f, 1f)
        if (handle != 0L) nativeSetLayerAlpha(handle, pendingLayerAlpha)
    }

    /** AOSP BlurFilter::drawBlurRegion compositing alpha. */
    @Synchronized
    fun setBlurAlpha(alpha: Float) {
        pendingBlurAlpha = alpha.coerceIn(0f, 1f)
        if (handle != 0L) nativeSetBlurAlpha(handle, pendingBlurAlpha)
    }

    /** AOSP backgroundBlurScale / zoom around blur center. */
    @Synchronized
    fun setBlurScale(scale: Float) {
        pendingBlurScale = scale.coerceAtLeast(0.1f)
        if (handle != 0L) nativeSetBlurScale(handle, pendingBlurScale)
    }

    /** AOSP BlurRegion list — per-rect blurred clips with rounded corners. */
    @Synchronized
    fun setBlurRegions(regions: List<BlurRegion>) {
        pendingRegions = regions.toTypedArray()
        if (handle != 0L) nativeSetBlurRegions(handle, pendingRegions)
    }

    @Synchronized
    fun clearBlurRegions() = setBlurRegions(emptyList())

    /**
     * AOSP [LayerSettings.blurRegionTransform] — column-major 3×3 affine applied before
     * [BlurRegion] draws. Use [BLUR_REGION_TRANSFORM_IDENTITY] for no transform.
     */
    @Synchronized
    fun setBlurRegionTransform(matrix: FloatArray) {
        require(matrix.size == 9) { "blurRegionTransform must have 9 elements" }
        pendingBlurRegionTransform = matrix.copyOf()
        if (handle != 0L) nativeSetBlurRegionTransform(handle, pendingBlurRegionTransform)
    }

    /** Vulkan glass rim over each [BlurRegion]. */
    @Synchronized
    fun setGlassRimEnabled(enabled: Boolean) {
        pendingGlassRimEnabled = enabled
        if (handle != 0L) nativeSetGlassRimEnabled(handle, enabled)
    }

    @Synchronized
    fun setGlassRimNightMode(night: Boolean) {
        pendingGlassRimNightMode = night
        if (handle != 0L) nativeSetGlassRimNightMode(handle, night)
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
        inputReady = true
        if (handle == 0L) {
            pendingBitmap = bitmap
            return
        }
        nativeSetInputBitmap(handle, bitmap)
    }

    /** Re-run Dual Kawase + present. Requires [hasInput] and a successful [attach]. */
    @Synchronized
    fun render() {
        if (handle != 0L && inputReady) nativeRender(handle)
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
        inputReady = false
    }

    @get:Synchronized
    val hasInput: Boolean
        get() = inputReady

    @get:Synchronized
    val isReady: Boolean
        get() = handle != 0L

    private external fun nativeCreate(surface: Surface, enableValidation: Boolean): Long
    private external fun nativeSetSurface(handle: Long, surface: Surface)
    private external fun nativeReleaseSurface(handle: Long)
    private external fun nativeResize(handle: Long, width: Int, height: Int)
    private external fun nativeSetRadius(handle: Long, radius: Float)
    private external fun nativeSetLayerAlpha(handle: Long, alpha: Float)
    private external fun nativeSetBlurAlpha(handle: Long, alpha: Float)
    private external fun nativeSetBlurScale(handle: Long, scale: Float)
    private external fun nativeSetBlurRegions(handle: Long, regions: Array<BlurRegion>)
    private external fun nativeSetBlurRegionTransform(handle: Long, matrix: FloatArray)
    private external fun nativeSetGlassRimEnabled(handle: Long, enabled: Boolean)
    private external fun nativeSetGlassRimNightMode(handle: Long, night: Boolean)
    private external fun nativeSetDebugLevel(handle: Long, level: Int)
    private external fun nativeSetInputBitmap(handle: Long, bitmap: Bitmap)
    private external fun nativeRender(handle: Long)
    private external fun nativeInfo(handle: Long): String
    private external fun nativeDownMs(handle: Long): Float
    private external fun nativeUpMs(handle: Long): Float
    private external fun nativeTotalMs(handle: Long): Float
    private external fun nativeDestroy(handle: Long)

    companion object {
        /** Longest input edge; matches native `kMaxWorkEdge`. */
        const val MAX_INPUT_EDGE = 1280

        /** Size of an auto-captured frame for [srcW]×[srcH] (keeps aspect, caps longest edge). */
        fun captureSize(srcW: Int, srcH: Int): Pair<Int, Int> {
            if (srcW <= 0 || srcH <= 0) return 0 to 0
            val edge = maxOf(srcW, srcH)
            if (edge <= MAX_INPUT_EDGE) return srcW to srcH
            val scale = MAX_INPUT_EDGE.toFloat() / edge
            return (srcW * scale).toInt().coerceAtLeast(1) to (srcH * scale).toInt().coerceAtLeast(1)
        }

        /** Column-major 3×3 identity (no region transform). */
        val BLUR_REGION_TRANSFORM_IDENTITY = floatArrayOf(
            1f, 0f, 0f,
            0f, 1f, 0f,
            0f, 0f, 1f,
        )

        init {
            check(captureSize(2560, 1440) == 1280 to 720)
            check(captureSize(720, 1280) == 720 to 1280)
            System.loadLibrary("vulkanblur")
        }
    }
}
