package com.vulkanfx.blur.demo

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Shader
import android.graphics.Typeface
import com.vulkanfx.blur.BlurRegion
import kotlin.math.cos
import kotlin.math.sin

/** Demo-only wallpaper bitmap for the sample app (not part of the library). */
object DemoScene {
    const val WALLPAPER_WIDTH = 720
    const val WALLPAPER_HEIGHT = 1280

    private data class CardTemplate(
        val cornerRadius: Float,
        val alpha: Float,
        val left: Int,
        val top: Int,
        val right: Int,
        val bottom: Int,
    )

    private val cardTemplates = listOf(
        CardTemplate(40f, 0.92f, 56, 180, 664, 420),
        CardTemplate(32f, 0.88f, 96, 500, 624, 720),
        CardTemplate(28f, 0.85f, 140, 820, 580, 1040),
    )

    /** AOSP-style blurRegions over the 720×1280 demo wallpaper. */
    fun cardBlurRegions(blurRadius: Int, phaseSec: Float = 0f): List<BlurRegion> {
        val motion = listOf(
            Pair(sin(phaseSec * 0.7f) * 48f, cos(phaseSec * 0.5f) * 36f),
            Pair(sin(phaseSec * 1.1f + 1f) * 56f, cos(phaseSec * 0.9f + 0.5f) * 32f),
            Pair(cos(phaseSec * 0.8f + 2f) * 44f, sin(phaseSec * 1.2f + 1f) * 48f),
        )
        return cardTemplates.mapIndexed { index, template ->
            val (dx, dy) = motion[index]
            BlurRegion(
                blurRadius = blurRadius,
                cornerRadiusTL = template.cornerRadius,
                cornerRadiusTR = template.cornerRadius,
                cornerRadiusBL = template.cornerRadius,
                cornerRadiusBR = template.cornerRadius,
                alpha = template.alpha,
                left = (template.left + dx).toInt(),
                top = (template.top + dy).toInt(),
                right = (template.right + dx).toInt(),
                bottom = (template.bottom + dy).toInt(),
            )
        }
    }

    /** Tall wallpaper for vertical scroll (two stacked panels, seamless loop). */
    fun tallWallpaper(): Bitmap {
        val bmp = Bitmap.createBitmap(WALLPAPER_WIDTH, WALLPAPER_HEIGHT * 2, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)
        drawWallpaperPanel(c, WALLPAPER_WIDTH, WALLPAPER_HEIGHT, yOffset = 0, panelIndex = 0)
        drawWallpaperPanel(c, WALLPAPER_WIDTH, WALLPAPER_HEIGHT, yOffset = WALLPAPER_HEIGHT, panelIndex = 1)
        return bmp
    }

    fun wallpaper(width: Int = WALLPAPER_WIDTH, height: Int = WALLPAPER_HEIGHT): Bitmap {
        val bmp = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        drawWallpaperPanel(Canvas(bmp), width, height, yOffset = 0, panelIndex = 0)
        return bmp
    }

    private fun drawWallpaperPanel(
        c: Canvas,
        width: Int,
        height: Int,
        yOffset: Int,
        panelIndex: Int,
    ) {
        val top = yOffset.toFloat()
        val bottom = top + height

        c.drawRect(
            0f, top, width.toFloat(), bottom,
            Paint().apply {
                shader = LinearGradient(
                    0f, top, width * 0.2f, bottom,
                    intArrayOf(
                        if (panelIndex == 0) 0xFF0D1B2A.toInt() else 0xFF1A1030.toInt(),
                        if (panelIndex == 0) 0xFF1B263B.toInt() else 0xFF2D1B4E.toInt(),
                        if (panelIndex == 0) 0xFF415A77.toInt() else 0xFF5C4D7A.toInt(),
                    ),
                    floatArrayOf(0f, 0.45f, 1f),
                    Shader.TileMode.CLAMP,
                )
            },
        )

        val glow = Paint(Paint.ANTI_ALIAS_FLAG)
        glow.shader = RadialGradient(
            width * (0.72f - panelIndex * 0.08f),
            top + height * 0.28f,
            width * 0.55f,
            intArrayOf(if (panelIndex == 0) 0xAAFF6B6B.toInt() else 0xAAFFB347.toInt(), Color.TRANSPARENT),
            floatArrayOf(0f, 1f),
            Shader.TileMode.CLAMP,
        )
        c.drawRect(0f, top, width.toFloat(), bottom, glow)

        val accent = Paint(Paint.ANTI_ALIAS_FLAG)
        accent.color = if (panelIndex == 0) 0xFF4CC9F0.toInt() else 0xFF06D6A0.toInt()
        c.drawCircle(width * 0.22f, top + height * 0.62f, width * 0.18f, accent)
        accent.color = if (panelIndex == 0) 0xFFF72585.toInt() else 0xFFEF476F.toInt()
        c.drawCircle(width * 0.78f, top + height * 0.58f, width * 0.14f, accent)
        accent.color = if (panelIndex == 0) 0xFF7209B7.toInt() else 0xFF118AB2.toInt()
        c.drawCircle(width * 0.5f, top + height * 0.38f, width * 0.22f, accent)

        val grid = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = 0x33FFFFFF
            strokeWidth = 2f
        }
        val step = width / 8f
        var x = step
        while (x < width) {
            c.drawLine(x, top, x, bottom, grid)
            x += step
        }
        var y = top + step
        while (y < bottom) {
            c.drawLine(0f, y, width.toFloat(), y, grid)
            y += step
        }

        c.drawText(
            if (panelIndex == 0) "Blur demo" else "Scroll panel",
            width * 0.08f,
            top + height * 0.12f,
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                color = Color.WHITE
                textSize = width * 0.11f
                typeface = Typeface.DEFAULT_BOLD
                setShadowLayer(8f, 0f, 4f, 0x99000000.toInt())
            },
        )
        c.drawText(
            "Dual Kawase · Vulkan",
            width * 0.08f,
            top + height * 0.17f,
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                color = 0xCCFFFFFF.toInt()
                textSize = width * 0.045f
            },
        )
    }
}
