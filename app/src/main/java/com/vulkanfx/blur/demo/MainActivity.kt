package com.vulkanfx.blur.demo

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Shader
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import com.vulkanfx.blur.VulkanBlurView

class MainActivity : Activity() {
    private var radius = DEFAULT_RADIUS
    private var debugLevel = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        radius = savedInstanceState?.getInt(KEY_RADIUS, DEFAULT_RADIUS) ?: DEFAULT_RADIUS
        debugLevel = savedInstanceState?.getInt(KEY_DEBUG, 0) ?: 0

        val status = TextView(this).apply {
            setTextColor(Color.WHITE)
            setBackgroundColor(0xCC000000.toInt())
            textSize = 12f
            setPadding(32, 48, 32, 32)
            typeface = Typeface.MONOSPACE
            text = "VulkanBlur: waiting for surface…"
        }
        val scene = makeSceneBitmap()
        val blurView = VulkanBlurView(this, blurRadius = radius.toFloat()).apply {
            this.debugLevel = debugLevel
            onStatus = { status.text = it }
            setInputBitmap(scene)
        }
        val radiusSlider = SeekBar(this).apply {
            max = 64
            progress = radius
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    radius = progress.coerceAtLeast(1)
                    blurView.blurRadius = radius.toFloat()
                }
                override fun onStartTrackingTouch(seekBar: SeekBar) {}
                override fun onStopTrackingTouch(seekBar: SeekBar) {}
            })
        }
        // 0 = final, 1..6 = downsample levels (clamped to current pass count in native).
        val debugSlider = SeekBar(this).apply {
            max = 6
            progress = debugLevel
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    debugLevel = progress
                    blurView.debugLevel = progress
                }
                override fun onStartTrackingTouch(seekBar: SeekBar) {}
                override fun onStopTrackingTouch(seekBar: SeekBar) {}
            })
        }
        val controls = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(0x88000000.toInt())
            setPadding(32, 16, 32, 16)
            addView(TextView(context).apply {
                text = "radius"
                setTextColor(Color.LTGRAY)
                textSize = 11f
            })
            addView(radiusSlider)
            addView(TextView(context).apply {
                text = "debugLevel (0=final)"
                setTextColor(Color.LTGRAY)
                textSize = 11f
            })
            addView(debugSlider)
        }
        setContentView(FrameLayout(this).apply {
            addView(
                blurView,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT,
                ),
            )
            addView(
                status,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ),
            )
            addView(
                controls,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ).apply { gravity = Gravity.BOTTOM },
            )
        })
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt(KEY_RADIUS, radius)
        outState.putInt(KEY_DEBUG, debugLevel)
    }

    private companion object {
        const val KEY_RADIUS = "blur_radius"
        const val KEY_DEBUG = "debug_level"
        const val DEFAULT_RADIUS = 24

        /** Simple high-contrast scene under the 1280 work-edge cap. */
        fun makeSceneBitmap(): Bitmap {
            val w = 720
            val h = 1280
            val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
            val c = Canvas(bmp)
            c.drawRect(
                0f, 0f, w.toFloat(), h.toFloat(),
                Paint().apply {
                    shader = LinearGradient(
                        0f, 0f, w.toFloat(), h.toFloat(),
                        intArrayOf(0xFF1A237E.toInt(), 0xFF00897B.toInt(), 0xFFFF8F00.toInt()),
                        null,
                        Shader.TileMode.CLAMP,
                    )
                },
            )
            val circle = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFE91E63.toInt() }
            c.drawCircle(w * 0.35f, h * 0.32f, 180f, circle)
            circle.color = 0xFF76FF03.toInt()
            c.drawCircle(w * 0.68f, h * 0.48f, 140f, circle)
            circle.color = 0xFF00BCD4.toInt()
            c.drawCircle(w * 0.5f, h * 0.7f, 200f, circle)
            c.drawText(
                "VulkanBlur",
                w * 0.12f,
                h * 0.18f,
                Paint(Paint.ANTI_ALIAS_FLAG).apply {
                    color = Color.WHITE
                    textSize = 96f
                    typeface = Typeface.DEFAULT_BOLD
                },
            )
            return bmp
        }
    }
}
