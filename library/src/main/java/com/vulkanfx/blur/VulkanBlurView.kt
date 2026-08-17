package com.vulkanfx.blur

import android.content.Context
import android.content.pm.ApplicationInfo
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.util.AttributeSet
import android.util.Log
import android.view.Choreographer
import android.view.SurfaceHolder
import android.view.SurfaceView

/**
 * SurfaceView host for [VulkanBlur].
 *
 * Provide content with [setInputBitmap] before the first frame is drawn.
 */
class VulkanBlurView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    var onStatus: ((String) -> Unit)? = null
    /** Called each frame with GPU timing in ms (-1 if unavailable). */
    var onFrameStats: ((downMs: Float, upMs: Float, totalMs: Float) -> Unit)? = null

    var blurRadius: Float = 24f
        set(value) {
            field = value.coerceAtLeast(1f)
            try {
                blur.setBlurRadius(field)
                requestRender()
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Vulkan setRadius failed", e)
                onStatus?.invoke("VulkanBlur setRadius failed:\n${e.message}")
            }
        }

    /** Layer alpha scales blur strength (AOSP backgroundBlurRadius *= color.a). */
    var layerAlpha: Float = 1f
        set(value) {
            field = value.coerceIn(0f, 1f)
            try {
                blur.setLayerAlpha(field)
                requestRender()
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Vulkan setLayerAlpha failed", e)
                onStatus?.invoke("VulkanBlur setLayerAlpha failed:\n${e.message}")
            }
        }

    /** Compositing alpha when drawing the blurred region. */
    var blurAlpha: Float = 1f
        set(value) {
            field = value.coerceIn(0f, 1f)
            try {
                blur.setBlurAlpha(field)
                requestRender()
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Vulkan setBlurAlpha failed", e)
                onStatus?.invoke("VulkanBlur setBlurAlpha failed:\n${e.message}")
            }
        }

    /** Zoom scale around blur center (AOSP backgroundBlurScale). */
    var blurScale: Float = 1f
        set(value) {
            field = value.coerceAtLeast(0.1f)
            try {
                blur.setBlurScale(field)
                requestRender()
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Vulkan setBlurScale failed", e)
                onStatus?.invoke("VulkanBlur setBlurScale failed:\n${e.message}")
            }
        }

    /** Rounded-rect blur clips (AOSP blurRegions). */
    var blurRegions: List<BlurRegion> = emptyList()
        set(value) {
            field = value
            try {
                blur.setBlurRegions(value)
                requestRender()
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Vulkan setBlurRegions failed", e)
                onStatus?.invoke("VulkanBlur setBlurRegions failed:\n${e.message}")
            }
        }

    /** 0 = final blur output; 1..N = downsample pyramid levels. */
    var debugLevel: Int = 0
        set(value) {
            field = value.coerceAtLeast(0)
            try {
                blur.setDebugLevel(field)
                requestRender()
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Vulkan setDebugLevel failed", e)
                onStatus?.invoke("VulkanBlur setDebugLevel failed:\n${e.message}")
            }
        }

    private val blur = VulkanBlur()
    private var frameScheduled = false
    private val frameCallback = Choreographer.FrameCallback {
        frameScheduled = false
        if (!blur.isReady || !blur.hasInput) return@FrameCallback
        try {
            blur.render()
            onFrameStats?.invoke(blur.downsampleMs, blur.upsampleMs, blur.totalMs)
            onStatus?.invoke(blur.info())
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Vulkan render failed", e)
            onStatus?.invoke("VulkanBlur render failed:\n${e.message}")
        }
    }

    init {
        holder.setFormat(PixelFormat.RGBA_8888)
        holder.addCallback(this)
    }

    /** Convenience constructor matching the conceptual `VulkanBlurView(blurRadius = …)` API. */
    constructor(context: Context, blurRadius: Float) : this(context) {
        this.blurRadius = blurRadius
    }

    fun setInputBitmap(bitmap: Bitmap) {
        try {
            blur.setInputBitmap(bitmap)
            requestRender()
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Vulkan setInputBitmap failed", e)
            onStatus?.invoke("VulkanBlur setInputBitmap failed:\n${e.message}")
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "Vulkan setInputBitmap bad bitmap", e)
            onStatus?.invoke("VulkanBlur setInputBitmap failed:\n${e.message}")
        }
    }

    fun requestRender() {
        if (frameScheduled || !blur.isReady || !blur.hasInput) return
        frameScheduled = true
        Choreographer.getInstance().postFrameCallback(frameCallback)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val surface = holder.surface
        if (surface == null || !surface.isValid) {
            onStatus?.invoke("VulkanBlur: invalid surface")
            return
        }
        try {
            val debug = context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
            blur.setBlurRadius(blurRadius)
            blur.setLayerAlpha(layerAlpha)
            blur.setBlurAlpha(blurAlpha)
            blur.setBlurScale(blurScale)
            if (blurRegions.isNotEmpty()) blur.setBlurRegions(blurRegions)
            blur.setDebugLevel(debugLevel)
            val info = blur.attach(surface, enableValidation = debug)
            onStatus?.invoke(info)
            requestRender()
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Vulkan init failed", e)
            onStatus?.invoke("VulkanBlur init failed:\n${e.message}")
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.i(TAG, "surfaceChanged ${width}x$height format=$format")
        if (!blur.isReady) return
        try {
            blur.resize(width, height)
            requestRender()
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Vulkan resize failed", e)
            onStatus?.invoke("VulkanBlur resize failed:\n${e.message}")
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Choreographer.getInstance().removeFrameCallback(frameCallback)
        frameScheduled = false
        blur.releaseSurface()
    }

    override fun onDetachedFromWindow() {
        Choreographer.getInstance().removeFrameCallback(frameCallback)
        frameScheduled = false
        blur.detach()
        super.onDetachedFromWindow()
    }

    private companion object {
        const val TAG = "VulkanBlur"
    }
}
