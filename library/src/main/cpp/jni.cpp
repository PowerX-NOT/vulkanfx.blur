#include "VulkanContext.h"

#include <android/native_window_jni.h>
#include <jni.h>

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

extern "C" JNIEXPORT jstring JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeInfo(JNIEnv* env, jobject, jlong handle) {
    VulkanContext* ctx = fromHandle(handle);
    if (!ctx) {
        throwState(env, "native handle is null");
        return nullptr;
    }
    return env->NewStringUTF(ctx->info().c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_vulkanfx_blur_VulkanBlur_nativeDestroy(JNIEnv* env, jobject, jlong handle) {
    (void)env;
    delete fromHandle(handle);
}
