package com.vulkanfx.blur.demo

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.CompoundButton
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import com.vulkanfx.blur.VulkanBlurView
import java.util.Locale
import kotlin.math.roundToInt

class MainActivity : Activity() {
    private var radius = DEFAULT_RADIUS
    private var debugLevel = 0
    private var showDebug = false
    private var showDeviceInfo = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        radius = savedInstanceState?.getInt(KEY_RADIUS, DEFAULT_RADIUS) ?: DEFAULT_RADIUS
        debugLevel = savedInstanceState?.getInt(KEY_DEBUG, 0) ?: 0
        showDebug = savedInstanceState?.getBoolean(KEY_SHOW_DEBUG, false) ?: false
        showDeviceInfo = savedInstanceState?.getBoolean(KEY_SHOW_INFO, false) ?: false

        val radiusValue = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 15f
            typeface = Typeface.DEFAULT_BOLD
        }
        val timingChip = TextView(this).apply {
            setTextColor(0xFFB8FFB8.toInt())
            textSize = 12f
            typeface = Typeface.MONOSPACE
            text = getString(R.string.timing_placeholder)
        }
        val hud = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundResource(R.drawable.hud_chip)
            setPadding(40, 28, 40, 28)
            addView(TextView(context).apply {
                text = getString(R.string.hud_title)
                setTextColor(0x99FFFFFF.toInt())
                textSize = 11f
            })
            addView(radiusValue)
            addView(timingChip)
        }

        val deviceInfo = TextView(this).apply {
            setTextColor(Color.WHITE)
            setBackgroundResource(R.drawable.hud_chip)
            textSize = 10f
            typeface = Typeface.MONOSPACE
            setPadding(32, 24, 32, 24)
            visibility = if (showDeviceInfo) View.VISIBLE else View.GONE
        }

        val blurView = VulkanBlurView(this, blurRadius = radius.toFloat()).apply {
            this.debugLevel = debugLevel
            setInputBitmap(DemoScene.wallpaper())
            onFrameStats = { down, up, total ->
                timingChip.text = formatTiming(down, up, total)
            }
            onStatus = { deviceInfo.text = it }
        }

        fun refreshRadiusLabel() {
            radiusValue.text = getString(R.string.radius_value, radius)
        }
        refreshRadiusLabel()

        val radiusSlider = SeekBar(this).apply {
            max = 64
            progress = radius
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    radius = progress.coerceAtLeast(1)
                    refreshRadiusLabel()
                    blurView.blurRadius = radius.toFloat()
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                override fun onStopTrackingTouch(seekBar: SeekBar?) {}
            })
        }

        val debugPanel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = if (showDebug) View.VISIBLE else View.GONE
        }
        val debugSlider = SeekBar(this).apply {
            max = 6
            progress = debugLevel
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    debugLevel = progress
                    blurView.debugLevel = progress
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                override fun onStopTrackingTouch(seekBar: SeekBar?) {}
            })
        }
        debugPanel.addView(TextView(this).apply {
            text = getString(R.string.debug_level_hint)
            setTextColor(0x99FFFFFF.toInt())
            textSize = 12f
        })
        debugPanel.addView(debugSlider)

        val controls = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundResource(R.drawable.panel_background)
            setPadding(40, 32, 40, 48)
            addView(TextView(context).apply {
                text = getString(R.string.control_radius)
                setTextColor(Color.WHITE)
                textSize = 14f
                typeface = Typeface.DEFAULT_BOLD
            })
            addView(radiusSlider)
            addView(Switch(context).apply {
                text = getString(R.string.show_debug)
                setTextColor(Color.WHITE)
                isChecked = showDebug
                setOnCheckedChangeListener { _: CompoundButton, checked: Boolean ->
                    showDebug = checked
                    debugPanel.visibility = if (checked) View.VISIBLE else View.GONE
                    if (!checked) {
                        debugLevel = 0
                        debugSlider.progress = 0
                        blurView.debugLevel = 0
                    }
                }
            })
            addView(debugPanel)
            addView(Switch(context).apply {
                text = getString(R.string.show_device_info)
                setTextColor(Color.WHITE)
                isChecked = showDeviceInfo
                setOnCheckedChangeListener { _: CompoundButton, checked: Boolean ->
                    showDeviceInfo = checked
                    deviceInfo.visibility = if (checked) View.VISIBLE else View.GONE
                }
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
                hud,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ).apply {
                    gravity = Gravity.TOP or Gravity.START
                    setMargins(32, 48, 32, 0)
                },
            )
            addView(
                deviceInfo,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ).apply {
                    gravity = Gravity.TOP or Gravity.END
                    setMargins(32, 160, 32, 0)
                },
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
        outState.putBoolean(KEY_SHOW_DEBUG, showDebug)
        outState.putBoolean(KEY_SHOW_INFO, showDeviceInfo)
    }

    private fun formatTiming(downMs: Float, upMs: Float, totalMs: Float): String {
        if (totalMs < 0f) return getString(R.string.timing_placeholder)
        fun fmt(v: Float) = String.format(Locale.US, "%.2f", v)
        return getString(
            R.string.timing_value,
            fmt(downMs),
            fmt(upMs),
            fmt(totalMs),
        )
    }

    private companion object {
        const val KEY_RADIUS = "blur_radius"
        const val KEY_DEBUG = "debug_level"
        const val KEY_SHOW_DEBUG = "show_debug"
        const val KEY_SHOW_INFO = "show_device_info"
        const val DEFAULT_RADIUS = 24
    }
}
