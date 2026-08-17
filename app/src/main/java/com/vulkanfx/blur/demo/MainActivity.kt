package com.vulkanfx.blur.demo

import android.app.Activity
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.CompoundButton
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.window.OnBackInvokedCallback
import com.vulkanfx.blur.VulkanBlurView

class MainActivity : Activity() {
    private var radius = DEFAULT_RADIUS
    private var debugLevel = 0
    private var showDebug = false
    private var showDeviceInfo = false
    private var onHome = true

    private val backInvokedCallback = OnBackInvokedCallback { handleBack() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        radius = savedInstanceState?.getInt(KEY_RADIUS, DEFAULT_RADIUS) ?: DEFAULT_RADIUS
        debugLevel = savedInstanceState?.getInt(KEY_DEBUG, 0) ?: 0
        showDebug = savedInstanceState?.getBoolean(KEY_SHOW_DEBUG, false) ?: false
        showDeviceInfo = savedInstanceState?.getBoolean(KEY_SHOW_INFO, false) ?: false
        onHome = savedInstanceState?.getBoolean(KEY_HOME, true) ?: true

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            onBackInvokedDispatcher.registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,
                backInvokedCallback,
            )
        }

        showScreen()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            super.onBackPressed()
            return
        }
        if (!onHome) {
            onHome = true
            showScreen()
        } else {
            super.onBackPressed()
        }
    }

    private fun handleBack() {
        if (!onHome) {
            onHome = true
            showScreen()
        } else {
            finish()
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) enableFullscreen()
    }

    private fun enableFullscreen() {
        window.setDecorFitsSystemWindows(false)
        window.attributes = window.attributes.apply {
            layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        val decor = window.decorView ?: return
        decor.post {
            window.insetsController?.let { controller ->
                controller.hide(WindowInsets.Type.statusBars())
                controller.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        }
    }

    private fun showScreen() {
        if (onHome) showHomeScreen() else showDemoScreen()
        enableFullscreen()
    }

    private fun showHomeScreen() {
        val title = TextView(this).apply {
            text = getString(R.string.home_title)
            textSize = 30f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(0xFF111827.toInt())
        }

        val subtitle = TextView(this).apply {
            text = getString(R.string.home_subtitle)
            textSize = 15f
            setTextColor(0xFF4B5563.toInt())
        }

        fun optionCard(
            text: String,
            enabled: Boolean,
            onClick: () -> Unit,
        ): TextView = TextView(this).apply {
            this.text = text
            textSize = 18f
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER_VERTICAL
            minHeight = 160
            setPadding(48, 0, 48, 0)
            setBackgroundResource(R.drawable.home_card_background)
            setTextColor(if (enabled) 0xFF111827.toInt() else 0xFF9CA3AF.toInt())
            isEnabled = enabled
            if (enabled) {
                setOnClickListener { onClick() }
            }
        }

        val options = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(
                optionCard(getString(R.string.option_test_blur), enabled = true) {
                    onHome = false
                    showScreen()
                },
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ),
            )
            addView(
                optionCard(getString(R.string.option_blur_alpha), enabled = false) {},
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ).apply { topMargin = 24 },
            )
            addView(
                optionCard(getString(R.string.option_card_clips), enabled = false) {},
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ).apply { topMargin = 24 },
            )
        }

        setContentView(
            LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setBackgroundColor(0xFFF8FAFC.toInt())
                setPadding(40, 48, 40, 48)
                addView(title)
                addView(
                    subtitle,
                    LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                    ).apply { topMargin = 8 },
                )
                addView(
                    options,
                    LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                    ).apply { topMargin = 36 },
                )
            },
        )
    }

    private fun showDemoScreen() {
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
            onStatus = { deviceInfo.text = it }
        }

        val radiusSlider = SeekBar(this).apply {
            max = 64
            progress = radius
            setProgressTintList(ColorStateList.valueOf(0xFFFFFFFF.toInt()))
            setThumbTintList(ColorStateList.valueOf(0xFFFFFFFF.toInt()))
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    radius = progress.coerceAtLeast(1)
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
                val accent = 0xFF34D399.toInt()
                thumbTintList = ColorStateList.valueOf(accent)
                trackTintList = ColorStateList.valueOf(accent)
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
                val accent = 0xFF34D399.toInt()
                thumbTintList = ColorStateList.valueOf(accent)
                trackTintList = ColorStateList.valueOf(accent)
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
                deviceInfo,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ).apply {
                    gravity = Gravity.TOP or Gravity.END
                    setMargins(32, 48, 32, 0)
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
        outState.putBoolean(KEY_HOME, onHome)
    }

    private companion object {
        const val KEY_RADIUS = "blur_radius"
        const val KEY_DEBUG = "debug_level"
        const val KEY_SHOW_DEBUG = "show_debug"
        const val KEY_SHOW_INFO = "show_device_info"
        const val KEY_HOME = "home_screen"
        const val DEFAULT_RADIUS = 24
    }
}
