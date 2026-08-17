# Vulkan Blur

Frosted glass for Android. Drop a view over your UI — it blurs whatever is behind it.

minSdk 30 · arm64-v8a · Vulkan 1.1+

## Install

Clone this repo (or copy `library/`) and depend on the module:

```kotlin
// settings.gradle.kts
include(":library")

// app/build.gradle.kts
dependencies {
    implementation(project(":library"))
}
```

## Use it

Put content first, blur view on top. That’s the whole integration.

```xml
<FrameLayout
    xmlns:android="http://schemas.android.com/apk/res/android"
    xmlns:app="http://schemas.android.com/apk/res-auto"
    android:layout_width="match_parent"
    android:layout_height="match_parent">

    <!-- wallpaper, list, map, whatever -->

    <com.vulkanfx.blur.VulkanBlurView
        android:layout_width="match_parent"
        android:layout_height="match_parent"
        app:blurRadius="24dp" />

</FrameLayout>
```

No bitmap. No capture code. It snapshots the window behind it and blurs on the GPU.

Same thing in code:

```kotlin
val blur = VulkanBlurView(this)          // radius 24, auto-captures
parent.addView(blur, MATCH_PARENT, MATCH_PARENT)
blur.blurRadius = 32f                    // optional
```

## Frosted cards

Point it at views. Coordinates are handled for you.

```kotlin
blur.blurRegions = listOf(
    blur.regionFor(card, cornerRadius = 32f),
)
blur.glassRimEnabled = true
```

Or build a region yourself (input-bitmap space):

```kotlin
BlurRegion(left = 48, top = 120, right = 672, bottom = 360, blurRadius = 24, cornerRadius = 32f)
```

## Your own bitmap

```kotlin
blur.setInputBitmap(bitmap)   // ARGB_8888, longest edge ≤ 1280. Turns auto-capture off.
```

## Knobs

All optional. XML attrs use the same names (`app:blurRadius`, …).

| | Default | |
|---|---|---|
| `blurRadius` | 24 | Strength in px (`24dp` in XML) |
| `layerAlpha` | 1 | Scales blur strength |
| `blurAlpha` | 1 | How opaque the blur draw is |
| `blurScale` | 1 | Zoom around center |
| `autoCapture` | **true** | Snapshot what’s behind this view |
| `glassRimEnabled` | false | Rim on each `BlurRegion` |
| `blurRegions` | none | Rounded clips; empty = full-frame blur |

Live / scrolling content: set `continuousRendering = true` and update in `onFrameUpdate` **before** the blur runs that frame.

```kotlin
blur.continuousRendering = true
blur.onFrameUpdate = {
    card.translationX = ...
    blur.blurRegions = listOf(blur.regionFor(card, cornerRadius = 32f))
    true
}
```

## Own the Surface?

`VulkanBlur` is the engine. You almost never need it — `VulkanBlurView` already attaches, renders, and tears down.

```kotlin
val blur = VulkanBlur()
blur.attach(surface)
blur.setInputBitmap(bitmap)
blur.render()
blur.detach()
```

## Demo

```bash
./gradlew :app:installDebug
```

Wallpaper sits behind the blur view. Three screens: full-frame blur, alpha/scale, and moving frosted cards.

## Limits

- Captures **this app’s window**, not other apps or the launcher wallpaper. That’s SurfaceFlinger’s `Window.setBackgroundBlurRadius`.
- **arm64-v8a** only today (`abiFilters` in `library/build.gradle.kts`).
- Bitmap input must be `ARGB_8888`, longest edge ≤ 1280. Auto-capture already scales.
