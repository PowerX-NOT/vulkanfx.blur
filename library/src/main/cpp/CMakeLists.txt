cmake_minimum_required(VERSION 3.22.1)
project(vulkanblur LANGUAGES CXX)

# Headers: NDK sysroot vulkan/vulkan.h (and vulkan_android.h).
# Library: NDK stub libvulkan.so; the real ICD is loaded from the device.
add_library(vulkanblur SHARED
    VulkanContext.cpp
    VulkanImage.cpp
    VulkanBuffer.cpp
    jni.cpp
)

target_include_directories(vulkanblur PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vulkanblur PRIVATE cxx_std_17)
target_compile_options(vulkanblur PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)

find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)
target_link_libraries(vulkanblur PRIVATE
    ${log-lib}
    ${android-lib}
    ${vulkan-lib}
)

# Phase 3+: compile GLSL with NDK glslc into SPIR-V and package as assets.
# ${ANDROID_NDK}/shader-tools/${ANDROID_HOST_TAG}/glslc
