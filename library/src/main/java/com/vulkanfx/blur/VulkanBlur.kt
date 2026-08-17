package com.vulkanfx.blur

import android.view.Surface

class VulkanBlur {
    private var handle: Long = 0

    @Synchronized
    fun attach(surface: Surface, enableValidation: Boolean): String {
        detach()
        handle = nativeCreate(surface, enableValidation)
        return nativeInfo(handle)
    }

    @Synchronized
    fun resize(width: Int, height: Int) {
        if (handle != 0L) nativeResize(handle, width, height)
    }

    @Synchronized
    fun info(): String = if (handle != 0L) nativeInfo(handle) else ""

    @Synchronized
    fun detach() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    @get:Synchronized
    val isReady: Boolean
        get() = handle != 0L

    private external fun nativeCreate(surface: Surface, enableValidation: Boolean): Long
    private external fun nativeResize(handle: Long, width: Int, height: Int): Unit
    private external fun nativeInfo(handle: Long): String
    private external fun nativeDestroy(handle: Long)

    companion object {
        init {
            System.loadLibrary("vulkanblur")
        }
    }
}
