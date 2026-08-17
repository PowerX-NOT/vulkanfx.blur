package com.vulkanfx.blur

/**
 * A rounded rect to blur. All four corners default to [cornerRadius].
 *
 * Coordinates are in the blur input (captured bitmap), not raw screen pixels.
 * Prefer [VulkanBlurView.regionFor] so you never compute that yourself.
 */
data class BlurRegion(
    val left: Int = 0,
    val top: Int = 0,
    val right: Int = 0,
    val bottom: Int = 0,
    val blurRadius: Int = 24,
    val cornerRadius: Float = 0f,
    val alpha: Float = 1f,
    val cornerRadiusTL: Float = cornerRadius,
    val cornerRadiusTR: Float = cornerRadius,
    val cornerRadiusBL: Float = cornerRadius,
    val cornerRadiusBR: Float = cornerRadius,
)
