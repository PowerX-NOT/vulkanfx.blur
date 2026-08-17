package com.vulkanfx.blur.demo

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.SeekBar
import android.widget.TextView
import com.vulkanfx.blur.VulkanBlurView

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val status = TextView(this).apply {
            setTextColor(Color.WHITE)
            setBackgroundColor(0xCC000000.toInt())
            textSize = 12f
            setPadding(32, 48, 32, 32)
            typeface = Typeface.MONOSPACE
            text = "VulkanBlur: waiting for surface…"
        }
        val blurView = VulkanBlurView(this).apply {
            onStatus = { status.text = it }
        }
        val slider = SeekBar(this).apply {
            max = 64
            progress = 24
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                    if (fromUser) blurView.blurRadius = progress.coerceAtLeast(1).toFloat()
                }
                override fun onStartTrackingTouch(seekBar: SeekBar) {}
                override fun onStopTrackingTouch(seekBar: SeekBar) {}
            })
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
                slider,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ).apply {
                    gravity = Gravity.BOTTOM
                    bottomMargin = 48
                    leftMargin = 32
                    rightMargin = 32
                },
            )
        })
    }
}
