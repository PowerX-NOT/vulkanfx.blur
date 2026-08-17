package com.vulkanfx.blur

import android.content.Context
import android.content.res.Configuration
import android.content.pm.ApplicationInfo
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.util.AttributeSet
import android.util.Log
import android.view.Choreographer
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewTreeObserver

/**
 * Drop this over your UI. It blurs whatever is behind it.
 *
 * XML: `<com.vulkanfx.blur.VulkanBlurView … app:blurRadius="24dp" />`
 * Code: `VulkanBlurView(context)` — auto-captures, radius 24.
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
                maybeRequestRender()
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Vulkan setBlurRegions failed", e)
                onStatus?.invoke("VulkanBlur setBlurRegions failed:\n${e.message}")
            }
        }

    /**
     * Snapshot window layers behind this view each frame.
     * On by default. [setInputBitmap] turns it off.
     */
    var autoCapture: Boolean = true
        set(value) {
            if (field == value) return
            field = value
            if (isAttachedToWindow) {
                if (value) viewTreeObserver.addOnPreDrawListener(preDrawListener)
                else viewTreeObserver.removeOnPreDrawListener(preDrawListener)
            }
            if (value) scheduleFrame()
        }

    /**
     * When true, re-render every vsync. Use with [onFrameUpdate] for animated content
     * (scroll + moving blur regions) so updates always run before [VulkanBlur.render].
     */
    var continuousRendering: Boolean = false
        set(value) {
            field = value
            if (value) scheduleFrame()
        }

    /** Called before each render while [continuousRendering] is on. Return false to skip the frame. */
    var onFrameUpdate: (() -> Boolean)? = null

    /** Layer transform applied before blurRegions (AOSP blurRegionTransform). */
    var blurRegionTransform: FloatArray = VulkanBlur.BLUR_REGION_TRANSFORM_IDENTITY
        set(value) {
            require(value.size == 9) { "blurRegionTransform must have 9 elements" }
            field = value.copyOf()
            try {
                blur.setBlurRegionTransform(field)
                requestRender()
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Vulkan setBlurRegionTransform failed", e)
                onStatus?.invoke("VulkanBlur setBlurRegionTransform failed:\n${e.message}")
            }
        }

    /** Vulkan glass rim over each [BlurRegion]. */
    var glassRimEnabled: Boolean = false
        set(value) {
            field = value
            try {
                blur.setGlassRimEnabled(value)
                requestRender()
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Vulkan setGlassRimEnabled failed", e)
                onStatus?.invoke("VulkanBlur setGlassRimEnabled failed:\n${e.message}")
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
    private val sceneCapture = SceneCapture()
    private var frameScheduled = false
    private var suppressRenderRequest = false
    private val preDrawListener = ViewTreeObserver.OnPreDrawListener {
        if (autoCapture) scheduleFrame()
        true
    }
    private val frameCallback = Choreographer.FrameCallback {
        frameScheduled = false
        if (!blur.isReady) {
            if (continuousRendering || autoCapture) scheduleFrame()
            return@FrameCallback
        }
        try {
            suppressRenderRequest = true
            val proceed = onFrameUpdate?.invoke() ?: true
            if (proceed && autoCapture) captureBehind()
            suppressRenderRequest = false
            if (!proceed) {
                if (continuousRendering || autoCapture) scheduleFrame()
                return@FrameCallback
            }
            if (!blur.hasInput) {
                if (continuousRendering || autoCapture) scheduleFrame()
                return@FrameCallback
            }
            blur.render()
            onFrameStats?.invoke(blur.downsampleMs, blur.upsampleMs, blur.totalMs)
            onStatus?.invoke(blur.info())
        } catch (e: IllegalStateException) {
            suppressRenderRequest = false
            Log.e(TAG, "Vulkan render failed", e)
            onStatus?.invoke("VulkanBlur render failed:\n${e.message}")
        }
        if (continuousRendering) scheduleFrame()
    }

    init {
        holder.setFormat(PixelFormat.RGBA_8888)
        holder.addCallback(this)
        applyAttrs(attrs)
    }

    /** Convenience constructor matching the conceptual `VulkanBlurView(blurRadius = …)` API. */
    constructor(context: Context, blurRadius: Float) : this(context) {
        this.blurRadius = blurRadius
    }

    /** Blur the area of [view] (window coords mapped into the captured input). */
    fun regionFor(
        view: View,
        blurRadius: Int = this.blurRadius.toInt(),
        cornerRadius: Float = 0f,
        alpha: Float = 1f,
    ): BlurRegion {
        val hostLoc = IntArray(2)
        val viewLoc = IntArray(2)
        getLocationInWindow(hostLoc)
        view.getLocationInWindow(viewLoc)
        val srcW = width.coerceAtLeast(1)
        val srcH = height.coerceAtLeast(1)
        val (cw, ch) = VulkanBlur.captureSize(srcW, srcH)
        val sx = cw / srcW.toFloat()
        val sy = ch / srcH.toFloat()
        val l = viewLoc[0] - hostLoc[0]
        val t = viewLoc[1] - hostLoc[1]
        return BlurRegion(
            left = (l * sx).toInt(),
            top = (t * sy).toInt(),
            right = ((l + view.width) * sx).toInt(),
            bottom = ((t + view.height) * sy).toInt(),
            blurRadius = blurRadius,
            cornerRadius = cornerRadius,
            alpha = alpha,
        )
    }

    /** Feed your own pixels. Turns [autoCapture] off. */
    fun setInputBitmap(bitmap: Bitmap) {
        if (autoCapture) autoCapture = false
        uploadInput(bitmap)
    }

    fun requestRender() = maybeRequestRender()

    private fun applyAttrs(attrs: AttributeSet?) {
        if (attrs == null) return
        val a = context.obtainStyledAttributes(attrs, R.styleable.VulkanBlurView)
        try {
            if (a.hasValue(R.styleable.VulkanBlurView_blurRadius)) {
                blurRadius = a.getDimension(R.styleable.VulkanBlurView_blurRadius, blurRadius)
            }
            if (a.hasValue(R.styleable.VulkanBlurView_layerAlpha)) {
                layerAlpha = a.getFloat(R.styleable.VulkanBlurView_layerAlpha, layerAlpha)
            }
            if (a.hasValue(R.styleable.VulkanBlurView_blurAlpha)) {
                blurAlpha = a.getFloat(R.styleable.VulkanBlurView_blurAlpha, blurAlpha)
            }
            if (a.hasValue(R.styleable.VulkanBlurView_blurScale)) {
                blurScale = a.getFloat(R.styleable.VulkanBlurView_blurScale, blurScale)
            }
            autoCapture = a.getBoolean(R.styleable.VulkanBlurView_autoCapture, true)
            glassRimEnabled = a.getBoolean(R.styleable.VulkanBlurView_glassRimEnabled, false)
        } finally {
            a.recycle()
        }
    }

    private fun uploadInput(bitmap: Bitmap) {
        try {
            blur.setInputBitmap(bitmap)
            maybeRequestRender()
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Vulkan setInputBitmap failed", e)
            onStatus?.invoke("VulkanBlur setInputBitmap failed:\n${e.message}")
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "Vulkan setInputBitmap bad bitmap", e)
            onStatus?.invoke("VulkanBlur setInputBitmap failed:\n${e.message}")
        }
    }

    private fun captureBehind() {
        val bmp = sceneCapture.snapshot(this) ?: return
        uploadInput(bmp)
        sceneCapture.snapshotSurfaceLayers(this) { layered ->
            if (layered != null) uploadInput(layered)
        }
    }

    private fun maybeRequestRender() {
        if (suppressRenderRequest) return
        if (!continuousRendering && !autoCapture && (!blur.isReady || !blur.hasInput)) return
        scheduleFrame()
    }

    private fun scheduleFrame() {
        if (frameScheduled) return
        if (!blur.hasInput && !continuousRendering && !autoCapture) return
        frameScheduled = true
        Choreographer.getInstance().postFrameCallback(frameCallback)
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        if (autoCapture) viewTreeObserver.addOnPreDrawListener(preDrawListener)
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
            blur.setBlurRegionTransform(blurRegionTransform)
            blur.setGlassRimEnabled(glassRimEnabled)
            val night = (resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) ==
                Configuration.UI_MODE_NIGHT_YES
            blur.setGlassRimNightMode(night)
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
        if (autoCapture) viewTreeObserver.removeOnPreDrawListener(preDrawListener)
        Choreographer.getInstance().removeFrameCallback(frameCallback)
        frameScheduled = false
        sceneCapture.release()
        blur.detach()
        super.onDetachedFromWindow()
    }

    private companion object {
        const val TAG = "VulkanBlur"
    }
}
