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

Update blur or content anytime:

```kotlin
blurView.blurRadius = 32f
blurView.setInputBitmap(updatedBitmap)
blurView.requestRender()  // optional; radius/bitmap changes already schedule a frame
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
| `setBlurRadius(radius)` | Blur strength; ≥ 1. Same scale as AOSP window blur radius. |
| `setDebugLevel(level)` | `0` = final output; `1..N` = downsample pyramid stage (debug). |
| `render()` | Record Kawase passes + blit to swapchain + present. |
| `resize(w, h)` | Swapchain recreate on surface size change. |
| `info()` | Multi-line status string (device, timings, pyramid). |
| `downsampleMs` / `upsampleMs` / `totalMs` | Last frame GPU timings (-1 if unsupported). |

## Radius behaviour (AOSP parity)

- Internally scaled by `× 0.57735` to match Skia Gaussian equivalence.
- **Radius &lt; 10**: full-resolution mix pass blends blurred output with the original (crossfade).
- **Radius ≥ 10**: blur stays at quarter resolution; present upsamples with linear filtering (same strategy as SF `drawBlurRegion`).

## Architecture

```
VulkanBlurView  →  VulkanBlur (Kotlin/JNI)  →  VulkanContext  →  KawaseBlur  →  compute shaders
                                                      ↓
                                            swapchain blit + present (one submit / frame)
```

Shaders: `kawase_down.comp`, `kawase_up.comp`, `kawase_draw.comp` (SPIR-V embedded at build time).

## Demo app

```bash
./gradlew :app:installDebug
```

The demo draws a synthetic wallpaper (`DemoScene` in `:app` only), exposes a radius slider, optional pyramid debug, and GPU timing HUD.

## Limitations

- **Blur-only** — no rounded-rect clip, layer alpha, or zoom transform (those are compositor concerns in AOSP `BlurFilter::drawBlurRegion`).
- **Bitmap in, Surface out** — you supply pixels; capturing behind-glass content is the host app’s job.
- **arm64-v8a** — extend `abiFilters` in `library/build.gradle.kts` to ship other ABIs.

## License

See repository license (if present). Algorithm reference: AOSP `frameworks/native/libs/renderengine/skia/filters/KawaseBlurDualFilterV2.cpp`.
