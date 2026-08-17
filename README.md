# Vulkan Blur (`com.vulkanfx.blur`)

Android library that blurs RGBA bitmaps with **Dual Kawase V2**, matching AOSP RenderEngine’s `KawaseBlurDualFilterV2` (`window_blur_kawase2_fix_aliasing`). Rendering is **Vulkan compute** on the GPU; output is presented to a `SurfaceView`.

The `:app` module is a small demo. All blur logic lives in `:library`.

## Requirements

| | |
|---|---|
| **minSdk** | 30 |
| **ABI** | `arm64-v8a` (only build today) |
| **GPU** | Vulkan 1.1+ with compute + swapchain present |
| **Input** | `Bitmap.Config.ARGB_8888`, longest edge ≤ **1280** px |

## Add the library

**Gradle (local module)** — clone this repo or copy `library/` into your project:

```kotlin
// settings.gradle.kts
include(":library")

// app/build.gradle.kts
dependencies {
    implementation(project(":library"))
}
```

**Maven** — not published yet; use the module dependency above.

## Quick start: `VulkanBlurView`

Drop a `SurfaceView` subclass into your layout (or build it in code), feed it a bitmap, and drive frames with Choreographer:

```kotlin
import com.vulkanfx.blur.VulkanBlurView

val blurView = VulkanBlurView(context).apply {
    blurRadius = 24f
    setInputBitmap(myArgb8888Bitmap)  // required before first frame
}

// Add blurView to your view hierarchy (e.g. FrameLayout).
// It renders automatically when the surface is ready.
```

Update blur, alpha, regions, or content anytime:

```kotlin
blurView.blurRadius = 32f
blurView.layerAlpha = 0.8f      // scales background blur radius (AOSP LayerSnapshotBuilder)
blurView.blurAlpha = 0.9f       // draw alpha for full-frame background blur
blurView.blurScale = 1.1f       // zoom around center (AOSP backgroundBlurScale)
blurView.blurRegions = listOf(
    BlurRegion(
        blurRadius = 24,
        cornerRadiusTL = 32f, cornerRadiusTR = 32f,
        cornerRadiusBL = 32f, cornerRadiusBR = 32f,
        alpha = 0.9f,
        left = 48, top = 120, right = 672, bottom = 360,
    ),
)
blurView.setInputBitmap(updatedBitmap)
blurView.requestRender()  // optional; property changes already schedule a frame
```

Optional callbacks:

```kotlin
blurView.onFrameStats = { downMs, upMs, totalMs ->
    // GPU timings in milliseconds, or -1 if unavailable
}
blurView.onStatus = { text ->
    // Full native info dump (device, pyramid, timings)
}
```

## Lower level: `VulkanBlur`

Use this when you own the `Surface` lifecycle (custom `SurfaceView`, `TextureView`, etc.):

```kotlin
import com.vulkanfx.blur.VulkanBlur

val blur = VulkanBlur()

// 1. Surface available
val info = blur.attach(surface, enableValidation = BuildConfig.DEBUG)

// 2. Input required before render
blur.setBlurRadius(24f)
blur.setInputBitmap(bitmap)

// 3. Each frame (e.g. Choreographer callback)
blur.render()

// 4. Teardown
blur.releaseSurface()  // surface lost
blur.detach()          // destroy Vulkan context
```

### API summary

| Method | Description |
|--------|-------------|
| `attach(surface)` | Create Vulkan device + swapchain for `surface`. |
| `setInputBitmap(bitmap)` | Upload ARGB_8888 pixels (max edge 1280). **Required** before `render()`. |
| `setBlurRadius(radius)` | Full-frame background blur (`backgroundBlurRadius` when no regions). Scaled by `layerAlpha`. |
| `setLayerAlpha(alpha)` | Scales effective background blur radius (`color.a * radius`, AOSP `LayerSnapshotBuilder`). |
| `setBlurAlpha(alpha)` | Compositing alpha when drawing full-frame background blur (`drawBlurRegion` alpha). |
| `setBlurScale(scale)` | Zoom around blur center (`backgroundBlurScale`). |
| `setBlurRegions(regions)` | Rounded-rect blurred clips (`BlurRegion` list, AOSP `blurRegions`). |
| `setDebugLevel(level)` | `0` = final output; `1..N` = downsample pyramid stage (debug). |
| `render()` | Record Kawase passes + blit to swapchain + present. |
| `resize(w, h)` | Swapchain recreate on surface size change. |
| `info()` | Multi-line status string (device, timings, pyramid). |
| `downsampleMs` / `upsampleMs` / `totalMs` | Last frame GPU timings (-1 if unsupported). |

## Radius behaviour (AOSP parity)

Matches `BlurFilter::drawBlurRegion` + `SkiaRenderEngine` blur path:

- Kawase generation uses the same `× 0.57735` sigma scale as AOSP `KawaseBlurDualFilterV2`.
- **Background blur**: `effectiveRadius = backgroundBlurRadius * layerAlpha` (or legacy `setBlurRadius` when no regions).
- **Blur regions**: per-region radius (not scaled by layer alpha); draw alpha = `region.alpha * layerAlpha`; scale = 1.
- **Radius &lt; 10**: crossfade between sharp input and blurred output (`mixFactor = radius / 10`).
- **Radius ≥ 10**: quarter-res Kawase pyramid; composite samples with linear filtering + optional zoom.
- **Rounded clips**: SDF rounded-rect mask per `BlurRegion` corner radii.

## Architecture

```
VulkanBlurView  →  VulkanBlur (Kotlin/JNI)  →  VulkanContext  →  BlurEngine  →  KawaseBlur (pyramid)
                                                      ↓                    ↓
                                            swapchain blit + present   kawase_composite.comp (drawBlurRegion)
```

Shaders: `kawase_down.comp`, `kawase_up.comp`, `kawase_draw.comp`, `kawase_composite.comp` (SPIR-V embedded at build time).

## Demo app

```bash
./gradlew :app:installDebug
```

The demo draws a synthetic wallpaper (`DemoScene` in `:app` only) and exposes three modes:

- **Test Blur** — full-frame background blur + radius slider
- **Blur + Alpha** — layer alpha, blur alpha, and blur scale sliders
- **Card Clips** — three rounded `BlurRegion` clips over the wallpaper

## AOSP parity scope

**Implemented (RenderEngine blur path):**

| AOSP | This library |
|------|----------------|
| `BlurFilter::drawBlurRegion` | `kawase_composite.comp` + `BlurEngine::drawBlurRegion` |
| `backgroundBlurRadius`, `backgroundBlurScale` | `setBlurRadius`, `setBlurScale` |
| `layerAlpha` → radius scale | `setLayerAlpha` |
| `BlurRegion` rounded clips | `setBlurRegions` / `BlurRegion` |
| Kawase V2 pyramid | `KawaseBlur` |

**Not in scope for an app/library module** (SurfaceFlinger / full system compositor):

- `blurRegionTransform` (identity assumed)
- `layerHasBlur` opaque-layer skip (no layer content pipeline)
- Live multi-layer snapshots (host supplies one bitmap)
- `Transaction.setBackgroundBlurRadius` / SF integration
- Actual `SurfaceFlinger` layer ownership, capture, scheduling, and composition policy

## Limitations

- **Bitmap in, Surface out** — you supply pixels; capturing behind-glass content is the host app’s job.
- **arm64-v8a** — extend `abiFilters` in `library/build.gradle.kts` to ship other ABIs.

## License

See repository license (if present). Algorithm reference: AOSP `frameworks/native/libs/renderengine/skia/filters/KawaseBlurDualFilterV2.cpp`.
