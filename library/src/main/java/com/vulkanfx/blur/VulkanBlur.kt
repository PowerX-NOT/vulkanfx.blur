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
    private external fun nativeInfo(handle: Long): String
    private external fun nativeDestroy(handle: Long)

    companion object {
        init {
            System.loadLibrary("vulkanblur")
        }
    }
}
