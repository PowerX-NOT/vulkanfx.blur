package com.vulkanfx.blur.demo

import android.app.Activity
import android.graphics.Color
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
        val blurView = VulkanBlurView(this, blurRadius = radius.toFloat()).apply {
            this.debugLevel = debugLevel
            onStatus = { status.text = it }
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
    }
}
