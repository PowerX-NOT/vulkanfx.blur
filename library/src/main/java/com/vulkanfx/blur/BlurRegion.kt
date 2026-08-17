package com.vulkanfx.blur

/**
 * Matches AOSP [android.ui.BlurRegion].
 *
 * @param blurRadius per-region blur strength before layer alpha scaling
 * @param alpha compositing alpha for [BlurFilter.drawBlurRegion] (pre-scale with layer alpha)
 */
data class BlurRegion(
    val blurRadius: Int = 0,
    val cornerRadiusTL: Float = 0f,
    val cornerRadiusTR: Float = 0f,
    val cornerRadiusBL: Float = 0f,
    val cornerRadiusBR: Float = 0f,
    val alpha: Float = 1f,
    val left: Int = 0,
    val top: Int = 0,
    val right: Int = 0,
    val bottom: Int = 0,
)
