#include "VulkanContext.h"

#include <android/bitmap.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <algorithm>
#include <vector>

namespace {

void throwState(JNIEnv* env, const char* msg) {
    env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), msg);
}

VulkanContext* fromHandle(jlong handle) {
    return reinterpret_cast<VulkanContext*>(handle);
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeCreate(JNIEnv* env, jobject, jobject surface, jboolean validation) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        throwState(env, "ANativeWindow_fromSurface failed");
        return 0;
    }
    std::string error;
    VulkanContext* ctx = VulkanContext::create(window, validation == JNI_TRUE, &error);
    if (!ctx) {
        throwState(env, error.empty() ? "VulkanContext::create failed" : error.c_str());
        return 0;
    }
    return reinterpret_cast<jlong>(ctx);
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetSurface(JNIEnv* env, jobject, jlong handle, jobject surface) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        throwState(env, "ANativeWindow_fromSurface failed");
        return;
    }
    if (!ctx->setSurface(window)) {
        throwState(env, ctx->lastError().empty() ? "setSurface failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeReleaseSurface(JNIEnv* env, jobject, jlong handle) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    ctx->releaseSurface();
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeResize(JNIEnv* env, jobject, jlong handle, jint width, jint height) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!ctx->resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        throwState(env, ctx->lastError().empty() ? "swapchain resize failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetRadius(JNIEnv* env, jobject, jlong handle, jfloat radius) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!ctx->setRadius(radius)) {
        throwState(env, ctx->lastError().empty() ? "setRadius failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetLayerAlpha(JNIEnv* env, jobject, jlong handle, jfloat alpha) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!ctx->setLayerAlpha(alpha)) {
        throwState(env, ctx->lastError().empty() ? "setLayerAlpha failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetBlurAlpha(JNIEnv* env, jobject, jlong handle, jfloat alpha) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!ctx->setBlurAlpha(alpha)) {
        throwState(env, ctx->lastError().empty() ? "setBlurAlpha failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetBlurScale(JNIEnv* env, jobject, jlong handle, jfloat scale) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!ctx->setBlurScale(scale)) {
        throwState(env, ctx->lastError().empty() ? "setBlurScale failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetBlurRegions(JNIEnv* env, jobject, jlong handle,
                                                       jobjectArray regions) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    std::vector<BlurRegion> native;
    if (regions != nullptr) {
        const jsize n = env->GetArrayLength(regions);
        native.reserve(static_cast<size_t>(n));
        for (jsize i = 0; i < n; ++i) {
            jobject item = env->GetObjectArrayElement(regions, i);
            if (!item) continue;
            jclass cls = env->GetObjectClass(item);
            auto intField = [&](const char* name) {
                return env->GetIntField(item, env->GetFieldID(cls, name, "I"));
            };
            auto floatField = [&](const char* name) {
                return env->GetFloatField(item, env->GetFieldID(cls, name, "F"));
            };
            BlurRegion r{};
            r.blurRadius = static_cast<uint32_t>(std::max(0, intField("blurRadius")));
            r.cornerRadiusTL = floatField("cornerRadiusTL");
            r.cornerRadiusTR = floatField("cornerRadiusTR");
            r.cornerRadiusBL = floatField("cornerRadiusBL");
            r.cornerRadiusBR = floatField("cornerRadiusBR");
            r.alpha = floatField("alpha");
            r.left = intField("left");
            r.top = intField("top");
            r.right = intField("right");
            r.bottom = intField("bottom");
            native.push_back(r);
            env->DeleteLocalRef(item);
            env->DeleteLocalRef(cls);
        }
    }
    if (!ctx->setBlurRegions(native)) {
        throwState(env, ctx->lastError().empty() ? "setBlurRegions failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetBlurRegionTransform(JNIEnv* env, jobject, jlong handle,
                                                                jfloatArray matrix) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (matrix == nullptr) {
        throwState(env, "blurRegionTransform is null");
        return;
    }
    if (env->GetArrayLength(matrix) != 9) {
        throwState(env, "blurRegionTransform must have 9 elements");
        return;
    }
    jfloat elements[9]{};
    env->GetFloatArrayRegion(matrix, 0, 9, elements);
    if (!ctx->setBlurRegionTransform(elements)) {
        throwState(env, ctx->lastError().empty() ? "setBlurRegionTransform failed"
                                                 : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetGlassRimEnabled(JNIEnv* env, jobject, jlong handle,
                                                            jboolean enabled) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!ctx->setGlassRimEnabled(enabled == JNI_TRUE)) {
        throwState(env, ctx->lastError().empty() ? "setGlassRimEnabled failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetGlassRimNightMode(JNIEnv* env, jobject, jlong handle,
                                                              jboolean night) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!ctx->setGlassRimNightMode(night == JNI_TRUE)) {
        throwState(env, ctx->lastError().empty() ? "setGlassRimNightMode failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetDebugLevel(JNIEnv* env, jobject, jlong handle, jint level) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!ctx->setDebugLevel(level)) {
        throwState(env, ctx->lastError().empty() ? "setDebugLevel failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeSetInputBitmap(JNIEnv* env, jobject, jlong handle, jobject bitmap) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!bitmap) {
        throwState(env, "bitmap is null");
        return;
    }
    AndroidBitmapInfo info{};
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        throwState(env, "AndroidBitmap_getInfo failed");
        return;
    }
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        throwState(env, "bitmap must be ARGB_8888 / RGBA_8888");
        return;
    }
    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS || !pixels) {
        throwState(env, "AndroidBitmap_lockPixels failed");
        return;
    }
    const bool ok = ctx->setInputRgba(static_cast<const uint8_t*>(pixels), info.width, info.height);
    AndroidBitmap_unlockPixels(env, bitmap);
    if (!ok) {
        throwState(env, ctx->lastError().empty() ? "setInputRgba failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeRender(JNIEnv* env, jobject, jlong handle) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return;
    }
    if (!ctx->render()) {
        throwState(env, ctx->lastError().empty() ? "render failed" : ctx->lastError().c_str());
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeInfo(JNIEnv* env, jobject, jlong handle) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return nullptr;
    }
    return env->NewStringUTF(ctx->info().c_str());
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeDownMs(JNIEnv* env, jobject, jlong handle) {
    (void)env;
    VulkanContext* ctx = fromHandle(handle);
    return ctx ? ctx->downMs() : -1.0f;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeUpMs(JNIEnv* env, jobject, jlong handle) {
    (void)env;
    VulkanContext* ctx = fromHandle(handle);
    return ctx ? ctx->upMs() : -1.0f;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeTotalMs(JNIEnv* env, jobject, jlong handle) {
    (void)env;
    VulkanContext* ctx = fromHandle(handle);
    return ctx ? ctx->totalMs() : -1.0f;
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeDestroy(JNIEnv* env, jobject, jlong handle) {
    (void)env;
    delete fromHandle(handle);
}
