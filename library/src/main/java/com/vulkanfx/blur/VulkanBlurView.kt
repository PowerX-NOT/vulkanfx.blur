package com.vulkanfx.blur

import android.content.Context
import android.content.pm.ApplicationInfo
import android.graphics.PixelFormat
import android.util.AttributeSet
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView

class VulkanBlurView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    var onStatus: ((String) -> Unit)? = null

    private val blur = VulkanBlur()

    init {
        holder.setFormat(PixelFormat.RGBA_8888)
        holder.addCallback(this)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val surface = holder.surface
        if (surface == null || !surface.isValid) {
            onStatus?.invoke("VulkanBlur: invalid surface")
            return
        }
        try {
            val debug = context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
            val info = blur.attach(surface, enableValidation = debug)
            onStatus?.invoke(info)
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
            onStatus?.invoke(blur.info())
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Vulkan resize failed", e)
            onStatus?.invoke("VulkanBlur resize failed:\n${e.message}")
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        blur.detach()
    }

    private companion object {
        const val TAG = "VulkanBlur"
    }
}
