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

target_include_directories(vulkanblur PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
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

set(_glslc "${ANDROID_NDK}/shader-tools/${ANDROID_NDK_HOST_SYSTEM_NAME}/glslc")
if(NOT EXISTS "${_glslc}")
    set(_glslc "${ANDROID_NDK}/shader-tools/linux-x86_64/glslc")
endif()
if(NOT EXISTS "${_glslc}")
    message(FATAL_ERROR "glslc not found under ${ANDROID_NDK}/shader-tools")
endif()

set(_copy_comp "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/copy.comp")
set(_copy_inc "${CMAKE_CURRENT_BINARY_DIR}/copy_comp_spv.inc")
add_custom_command(
    OUTPUT "${_copy_inc}"
    COMMAND "${_glslc}" -fshader-stage=compute -mfmt=c "${_copy_comp}" -o "${_copy_inc}"
    DEPENDS "${_copy_comp}"
    COMMENT "SPIR-V copy.comp"
    VERBATIM
)
set_source_files_properties(VulkanContext.cpp PROPERTIES OBJECT_DEPENDS "${_copy_inc}")
add_custom_target(copy_shader DEPENDS "${_copy_inc}")
add_dependencies(vulkanblur copy_shader)
