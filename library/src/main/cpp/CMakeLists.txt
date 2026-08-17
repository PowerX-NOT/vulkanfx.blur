cmake_minimum_required(VERSION 3.22.1)
project(vulkanblur LANGUAGES CXX)

# Headers: NDK sysroot vulkan/vulkan.h (and vulkan_android.h).
# Library: NDK stub libvulkan.so; the real ICD is loaded from the device.
add_library(vulkanblur SHARED
    VulkanContext.cpp
    VulkanImage.cpp
    VulkanBuffer.cpp
    KawaseBlur.cpp
    GlassPass.cpp
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

set(_shader_incs "")
macro(embed_comp NAME)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/${NAME}.comp")
    set(_inc "${CMAKE_CURRENT_BINARY_DIR}/${NAME}_comp_spv.inc")
    add_custom_command(
        OUTPUT "${_inc}"
        COMMAND "${_glslc}" -fshader-stage=compute -mfmt=c "${_src}" -o "${_inc}"
        DEPENDS "${_src}"
        COMMENT "SPIR-V ${NAME}.comp"
        VERBATIM
    )
    list(APPEND _shader_incs "${_inc}")
endmacro()

embed_comp(kawase_down)
embed_comp(kawase_up)
embed_comp(glass)
set_source_files_properties(KawaseBlur.cpp PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_BINARY_DIR}/kawase_down_comp_spv.inc;${CMAKE_CURRENT_BINARY_DIR}/kawase_up_comp_spv.inc")
set_source_files_properties(GlassPass.cpp PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_BINARY_DIR}/glass_comp_spv.inc")
add_custom_target(shaders DEPENDS ${_shader_incs})
add_dependencies(vulkanblur shaders)
