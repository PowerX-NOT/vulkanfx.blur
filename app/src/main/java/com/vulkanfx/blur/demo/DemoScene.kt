package com.vulkanfx.blur.demo

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Shader
import android.graphics.Typeface

/** Demo-only wallpaper bitmap for the sample app (not part of the library). */
object DemoScene {
    fun wallpaper(width: Int = 720, height: Int = 1280): Bitmap {
        val bmp = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)

        c.drawRect(
            0f, 0f, width.toFloat(), height.toFloat(),
            Paint().apply {
                shader = LinearGradient(
                    0f, 0f, width * 0.2f, height.toFloat(),
                    intArrayOf(0xFF0D1B2A.toInt(), 0xFF1B263B.toInt(), 0xFF415A77.toInt()),
                    floatArrayOf(0f, 0.45f, 1f),
                    Shader.TileMode.CLAMP,
                )
            },
        )

        val glow = Paint(Paint.ANTI_ALIAS_FLAG)
        glow.shader = RadialGradient(
            width * 0.72f, height * 0.28f, width * 0.55f,
            intArrayOf(0xAAFF6B6B.toInt(), Color.TRANSPARENT),
            floatArrayOf(0f, 1f),
            Shader.TileMode.CLAMP,
        )
        c.drawRect(0f, 0f, width.toFloat(), height.toFloat(), glow)

        val accent = Paint(Paint.ANTI_ALIAS_FLAG)
        accent.color = 0xFF4CC9F0.toInt()
        c.drawCircle(width * 0.22f, height * 0.62f, width * 0.18f, accent)
        accent.color = 0xFFF72585.toInt()
        c.drawCircle(width * 0.78f, height * 0.58f, width * 0.14f, accent)
        accent.color = 0xFF7209B7.toInt()
        c.drawCircle(width * 0.5f, height * 0.38f, width * 0.22f, accent)

        val grid = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = 0x33FFFFFF
            strokeWidth = 2f
        }
        val step = width / 8f
        var x = step
        while (x < width) {
            c.drawLine(x, 0f, x, height.toFloat(), grid)
            x += step
        }
        var y = step
        while (y < height) {
            c.drawLine(0f, y, width.toFloat(), y, grid)
            y += step
        }

        c.drawText(
            "Blur demo",
            width * 0.08f,
            height * 0.12f,
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
            height * 0.17f,
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                color = 0xCCFFFFFF.toInt()
                textSize = width * 0.045f
            },
        )
        return bmp
    }
}
