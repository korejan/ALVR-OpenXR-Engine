# Copyright (c) 2017-2025 The Khronos Group Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

if(POLICY CMP0075)
    cmake_policy(SET CMP0075 NEW)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(StdFilesystemFlags)

### Dependencies

set(XR_USE_GRAPHICS_API_OPENGL FALSE)
set(XR_USE_GRAPHICS_API_OPENGL_ES FALSE)
set(XR_USE_GRAPHICS_API_VULKAN FALSE)
set(XR_USE_GRAPHICS_API_D3D11 FALSE)
set(XR_USE_GRAPHICS_API_D3D12 FALSE)
set(XR_USE_GRAPHICS_API_METAL FALSE)

set(OPENGLES_INCOMPATIBLE TRUE)
set(OPENGL_INCOMPATIBLE FALSE)
set(VULKAN_INCOMPATIBLE FALSE)
set(METAL_INCOMPATIBLE TRUE)

set(XR_TARGET_CPU_ARCH ${CMAKE_SYSTEM_PROCESSOR})
set(XR_IS_UWP FALSE)
set(IS_TARGET_MSVC_ARM FALSE)
if (WIN32)
    if(MSVC_CXX_ARCHITECTURE_ID MATCHES "^ARM") # MSVC_C_ARCHITECTURE_ID MATCHES "^ARM" OR ))
        set(IS_TARGET_MSVC_ARM TRUE)
    endif()
    set(XR_TARGET_CPU_ARCH ${MSVC_CXX_ARCHITECTURE_ID})
endif()

# CMake will detect OpenGL/Vulkan which are not compatible with UWP and ARM/ARM64 on Windows so skip it in these cases.
if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore"
   OR IS_TARGET_MSVC_ARM
   OR (WIN32 AND CMAKE_GENERATOR_PLATFORM_UPPER MATCHES "ARM.*")
)
    set(OPENGL_INCOMPATIBLE TRUE)
    set(VULKAN_INCOMPATIBLE TRUE)
    set(XR_IS_UWP TRUE)
    message(STATUS "OpenGL/Vulkan disabled due to incompatibility")

elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(OPENGL_INCOMPATIBLE TRUE)
    message(STATUS "OpenGL disabled as no bindings exist for OpenGL on Darwin")
    set(METAL_INCOMPATIBLE FALSE)
elseif(ANDROID)
    set(OPENGL_INCOMPATIBLE TRUE)
    find_path(
        ANDROID_NATIVE_APP_GLUE android_native_app_glue.h
        PATHS "${ANDROID_NDK}/sources/android/native_app_glue"
    )
    if(ANDROID_NATIVE_APP_GLUE)
        # Needed by gfxwrapper
        set(OPENGLES_INCOMPATIBLE FALSE)
    endif()
    if(ANDROID_PLATFORM_LEVEL LESS 24)
        set(VULKAN_INCOMPATIBLE TRUE)
        message(
            STATUS
                "Vulkan disabled due to incompatibility: need to target at least API 24"
        )
    endif()
endif()

set(OPENGLES_INCOMPATIBLE TRUE)
set(OPENGL_INCOMPATIBLE TRUE)

if(NOT OPENGL_INCOMPATIBLE)
    set(OpenGL_GL_PREFERENCE GLVND)
    find_package(OpenGL)

    if(OPENGL_FOUND)
        set(XR_USE_GRAPHICS_API_OPENGL TRUE)
        add_definitions(-DXR_USE_GRAPHICS_API_OPENGL)
        message(STATUS "Enabling OpenGL support")
    elseif(BUILD_ALL_EXTENSIONS)
        message(FATAL_ERROR "OpenGL not found")
    endif()
endif()

if(NOT OPENGLES_INCOMPATIBLE)
    find_package(OpenGLES COMPONENTS V3 V2)
    find_package(EGL)
    if(OPENGLES_FOUND AND EGL_FOUND)
        set(XR_USE_GRAPHICS_API_OPENGL_ES TRUE)
        add_definitions(-DXR_USE_GRAPHICS_API_OPENGL_ES)
        message(STATUS "Enabling OpenGL|ES support")
    elseif(BUILD_ALL_EXTENSIONS)
        message(FATAL_ERROR "OpenGL|ES not found")
    endif()
endif()

if(NOT VULKAN_INCOMPATIBLE)
    # Find the Vulkan headers
    find_package(Vulkan)
    if(Vulkan_FOUND)
        set(XR_USE_GRAPHICS_API_VULKAN TRUE)
        add_definitions(-DXR_USE_GRAPHICS_API_VULKAN)
        message(STATUS "Enabling Vulkan support")
    elseif(BUILD_ALL_EXTENSIONS)
        message(FATAL_ERROR "Vulkan headers not found")
    endif()
endif()

if(NOT METAL_INCOMPATIBLE)
    find_package(MetalTools)
    if(MetalTools_FOUND)
        set(XR_USE_GRAPHICS_API_METAL TRUE)
        add_definitions(-DXR_USE_GRAPHICS_API_METAL)
        message(STATUS "Enabling Metal support")
    elseif(BUILD_ALL_EXTENSIONS)
        message(FATAL_ERROR "Metal not found - full Xcode install required")
    else()
        message(STATUS "Metal not found - full Xcode install required")
    endif()
endif()

find_package(Threads REQUIRED)
find_package(JsonCpp)

### All options defined here
option(BUILD_LOADER "Build loader" ON)
option(BUILD_ALL_EXTENSIONS "Build loader and layers with all extensions" ON)
option(
    BUILD_LOADER_WITH_EXCEPTION_HANDLING
    "Enable exception handling in the loader. Leave this on unless your standard library is built to not throw."
    ON
)
option(USE_OCULUS_OXR_EXT_HEADERS "Use the Oculus Mobile OpenXR SDK extension HEADERS only, for e.g. pure (air)link" OFF)
option(USE_OCULUS_MOBILE_LOADER "Links to the Oculus Mobile OpenXR SDK to use this loader (BUILD_LOADER will be ignored)" OFF)
option(USE_PICO_MOBILE_LOADER "Links to the Pico 4/Neo 3 OpenXR SDK (PUI >= 5.2.x) to use this loader (BUILD_LOADER will be ignored)" OFF)

if(WIN32)
    set(OPENXR_DEBUG_POSTFIX
        d
        CACHE STRING "OpenXR loader debug postfix."
    )
else()
    set(OPENXR_DEBUG_POSTFIX
        ""
        CACHE STRING "OpenXR loader debug postfix."
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    option(DYNAMIC_LOADER "Build the loader as a .dll library" OFF)
else()
    option(DYNAMIC_LOADER "Build the loader as a .dll library" ON)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/api_layers/CMakeLists.txt")
    option(BUILD_API_LAYERS "Build API layers" OFF)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    option(BUILD_TESTS "Build tests" OFF)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/CMakeLists.txt")
    option(BUILD_CONFORMANCE_TESTS "Build conformance tests" OFF)
endif()
include(CMakeDependentOption)

cmake_dependent_option(
    BUILD_WITH_SYSTEM_JSONCPP
    "Use system jsoncpp instead of vendored source"
    ON
    "JSONCPP_FOUND"
    OFF
)
cmake_dependent_option(
    BUILD_WITH_STD_FILESYSTEM
    "Use std::[experimental::]filesystem."
    ON
    "HAVE_FILESYSTEM_WITHOUT_LIB OR HAVE_FILESYSTEM_NEEDING_LIBSTDCXXFS OR HAVE_FILESYSTEM_NEEDING_LIBCXXFS"
    OFF
)

# Several files use these compile time OS switches
if(WIN32)
    add_definitions(-DXR_OS_WINDOWS)
    add_definitions(-DNOMINMAX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_definitions(-DXR_OS_LINUX)
elseif(ANDROID)
    add_definitions(-DXR_OS_ANDROID)
elseif(APPLE)
    add_definitions(-DXR_OS_APPLE)
endif()

# /EHsc (support for C++ exceptions) is default in most configurations but seems missing when building arm/arm64.
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()

# This is a little helper library for setting up OpenGL
if((OPENGL_FOUND OR OpenGLES_FOUND)
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/common/gfxwrapper_opengl.c"
)
    add_library(
        openxr-gfxwrapper STATIC common/gfxwrapper_opengl.c
                                 common/gfxwrapper_opengl.h
    )
    target_include_directories(
        openxr-gfxwrapper PUBLIC "${PROJECT_SOURCE_DIR}/external/include"
    )
    if(OPENGL_FOUND)
        if(TARGET OpenGL::OpenGL)
            target_link_libraries(openxr-gfxwrapper PUBLIC OpenGL::OpenGL)
        elseif(TARGET OpenGL::GL)
            target_link_libraries(openxr-gfxwrapper PUBLIC OpenGL::GL)
        else()
            target_link_libraries(openxr-gfxwrapper PUBLIC ${OPENGL_LIBRARIES})
        endif()
    endif()
    if(OpenGLES_FOUND)
        if(TARGET OpenGLES::OpenGLESv3)
            target_link_libraries(openxr-gfxwrapper PUBLIC OpenGLES::OpenGLESv3)
        elseif(TARGET OpenGLES::OpenGLESv2)
            target_link_libraries(openxr-gfxwrapper PUBLIC OpenGLES::OpenGLESv2)
        else()
            message(FATAL_ERROR "Should not get here!")
        endif()
        target_link_libraries(openxr-gfxwrapper PUBLIC EGL::EGL)
    endif()
    if(ANDROID)
        target_include_directories(
            openxr-gfxwrapper PUBLIC "${ANDROID_NATIVE_APP_GLUE}"
        )

        # Note: For some reason, just adding this to the gfxwrapper library results in failure at load time.
        # So, each consuming target must add $<TARGET_OBJECTS:android_native_app_glue> to their sources
        add_library(
            android_native_app_glue OBJECT
            "${ANDROID_NATIVE_APP_GLUE}/android_native_app_glue.c"
        )
        target_include_directories(
            android_native_app_glue PUBLIC "${ANDROID_NATIVE_APP_GLUE}"
        )
        target_compile_options(
            android_native_app_glue PRIVATE -Wno-unused-parameter
        )
    elseif(APPLE)
        target_compile_options(openxr-gfxwrapper PRIVATE -x objective-c++)
        target_link_libraries(
            openxr-gfxwrapper
            PUBLIC "-framework AppKit" "-framework Foundation"
                   "-framework CoreGraphics"
        )
    endif()
    set_target_properties(openxr-gfxwrapper PROPERTIES FOLDER ${HELPER_FOLDER})
    message(
        STATUS
            "Enabling OpenGL support in hello_xr, loader_test, and conformance, if configured"
    )
elseif(ANDROID)
    add_library(android_native_app_glue STATIC
        "${ANDROID_NATIVE_APP_GLUE}/android_native_app_glue.c"
    )
    target_include_directories(
        android_native_app_glue PUBLIC "${ANDROID_NATIVE_APP_GLUE}"
    )
    target_compile_options(
        android_native_app_glue PRIVATE -Wno-unused-parameter
    )
endif()

# Determine the presentation backend for Linux systems.
# Use an include because the code is pretty big.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(presentation)
endif()

# Several files use these compile time platform switches
if(WIN32)
    add_definitions(-DXR_USE_PLATFORM_WIN32)
elseif(APPLE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(DARWIN_TARGET_OS_NAME STREQUAL "ios")
            add_definitions(-DXR_USE_PLATFORM_IOS)
        else()
            add_definitions(-DXR_USE_PLATFORM_MACOS)
        endif()
    endif()
elseif(ANDROID)
    add_definitions(-DXR_USE_PLATFORM_ANDROID)
    set(OPENXR_ANDROID_VERSION_SUFFIX
        ""
        CACHE STRING "Suffix for generated Android artifacts."
    )
elseif(PRESENTATION_BACKEND MATCHES "xlib")
    add_definitions(-DXR_USE_PLATFORM_XLIB)
elseif(PRESENTATION_BACKEND MATCHES "xcb")
    add_definitions(-DXR_USE_PLATFORM_XCB)

    # TODO remove once conformance supports XCB
    set(BUILD_CONFORMANCE_TESTS OFF)
elseif(PRESENTATION_BACKEND MATCHES "wayland")
    add_definitions(-DXR_USE_PLATFORM_WAYLAND)

    # TODO remove once conformance supports Wayland
    set(BUILD_CONFORMANCE_TESTS OFF)
endif()

if(BUILD_CONFORMANCE_TESTS AND NOT ANDROID)
    set(BUILD_CONFORMANCE_CLI ON)
else()
    set(BUILD_CONFORMANCE_CLI OFF)
endif()

set(OPENXR_ALL_SUPPORTED_DEFINES)
if(BUILD_WITH_XLIB_HEADERS)
    list(APPEND OPENXR_ALL_SUPPORTED_DEFINES XR_USE_PLATFORM_XLIB)
endif()

if(BUILD_WITH_XCB_HEADERS)
    list(APPEND OPENXR_ALL_SUPPORTED_DEFINES XR_USE_PLATFORM_XCB)
endif()

if(BUILD_WITH_WAYLAND_HEADERS)
    list(APPEND OPENXR_ALL_SUPPORTED_DEFINES XR_USE_PLATFORM_WAYLAND)
endif()

# Find glslc shader compiler.
# On Android, the NDK includes the binary, so no external dependency.
if (OPENGL_INCOMPATIBLE AND VULKAN_INCOMPATIBLE)
    function(compile_glsl run_target_name)
        set(glsl_output_files "")
        add_custom_target(${run_target_name} ALL DEPENDS ${glsl_output_files})
        set_target_properties(${run_target_name} PROPERTIES FOLDER ${HELPER_FOLDER})
    endfunction()
else()
    if(ANDROID)
        file(GLOB glslc_folders ${ANDROID_NDK}/shader-tools/*)
        find_program(
            GLSL_COMPILER glslc
            PATHS ${glslc_folders}
            NO_DEFAULT_PATH
        )
    else()
        file(GLOB glslc_folders $ENV{VULKAN_SDK}/*)
        find_program(GLSL_COMPILER glslc PATHS ${glslc_folders})
    endif()
    find_program(GLSLANG_VALIDATOR glslangValidator)
    if(GLSL_COMPILER)
        message(NOTICE "Found glslc: ${GLSL_COMPILER}")
    elseif(GLSLANG_VALIDATOR)
        message(NOTICE "Found glslangValidator: ${GLSLANG_VALIDATOR}")
    else()
        message(NOTICE "Could NOT find glslc, using precompiled .spv files")
    endif()

    function(compile_glsl run_target_name)
        if(GLSL_COMPILER)
            set(GLSL_FLAGS -Werror -O -mfmt=c --target-env=vulkan1.1)
        else() # ==GLSLANG_VALIDATOR
            set(GLSL_FLAGS -g0 -V --target-env vulkan1.1)
        endif()

        set(glsl_output_files "")
        foreach(in_file IN LISTS ARGN)        
            get_filename_component(glsl_file ${in_file} NAME_WE)
            string(REGEX REPLACE "([A-Za-z0-9]*)[_]" "" glsl_stage ${glsl_file})
            
            set(out_file ${CMAKE_CURRENT_BINARY_DIR}/shaders/${glsl_file}.spv)
            set(out_file2 ${CMAKE_CURRENT_BINARY_DIR}/shaders/multiview/${glsl_file}.spv)
            if (glsl_stage STREQUAL "frag")
                set(out_file3 ${CMAKE_CURRENT_BINARY_DIR}/shaders/fovDecode/${glsl_file}.spv)
                set(out_file4 ${CMAKE_CURRENT_BINARY_DIR}/shaders/multiview/fovDecode/${glsl_file}.spv)
            endif()

            if(GLSL_COMPILER)
                # Run glslc if we can find it
                add_custom_command(
                    OUTPUT ${out_file}
                    OUTPUT ${out_file2}
                    COMMAND ${GLSL_COMPILER} ${GLSL_FLAGS} -fshader-stage=${glsl_stage} ${in_file} -o ${out_file}
                    COMMAND ${GLSL_COMPILER} ${GLSL_FLAGS} -DENABLE_MULTIVEW_EXT -fshader-stage=${glsl_stage} ${in_file} -o ${out_file2}
                    DEPENDS ${in_file}
                )
                if (glsl_stage STREQUAL "frag")
                    add_custom_command(                        
                        OUTPUT ${out_file3}
                        OUTPUT ${out_file4}
                        COMMAND ${GLSL_COMPILER} ${GLSL_FLAGS} -DENABLE_FOVEATION_DECODE -fshader-stage=${glsl_stage} ${in_file} -o ${out_file3}
                        COMMAND ${GLSL_COMPILER} ${GLSL_FLAGS} -DENABLE_MULTIVEW_EXT -DENABLE_FOVEATION_DECODE -fshader-stage=${glsl_stage} ${in_file} -o ${out_file4}
                        DEPENDS ${in_file}
                        #VERBATIM
                    )
                endif()
            elseif(GLSLANG_VALIDATOR)
                # Run glslangValidator if we can find it
                add_custom_command(
                    OUTPUT ${out_file}
                    OUTPUT ${out_file2}
                    COMMAND ${GLSLANG_VALIDATOR} ${GLSL_FLAGS} -S ${glsl_stage} ${in_file} -x -o ${out_file}
                    COMMAND ${GLSLANG_VALIDATOR} ${GLSL_FLAGS} -DENABLE_MULTIVEW_EXT -S ${glsl_stage} ${in_file} -x -o ${out_file2}
                    DEPENDS ${in_file}
                    VERBATIM
                )
                if (glsl_stage STREQUAL "frag")
                    add_custom_command(
                        OUTPUT ${out_file3}
                        OUTPUT ${out_file4}
                        COMMAND ${GLSLANG_VALIDATOR} ${GLSL_FLAGS} -DENABLE_FOVEATION_DECODE -S ${glsl_stage} ${in_file} -x -o ${out_file3}
                        COMMAND ${GLSLANG_VALIDATOR} ${GLSL_FLAGS} -DENABLE_MULTIVEW_EXT -DENABLE_FOVEATION_DECODE -S ${glsl_stage} ${in_file} -x -o ${out_file4}
                        DEPENDS ${in_file}
                        VERBATIM
                    )
                endif()
            else()
                # Use the precompiled .spv files
                get_filename_component(glsl_src_dir ${in_file} DIRECTORY)
                set(glsl_precompiled_dir ${glsl_src_dir}/precompiled)

                set(precompiled_file ${glsl_precompiled_dir}/${glsl_file}.spv)
                configure_file(${precompiled_file} ${out_file} COPYONLY)

                set(precompiled_file ${glsl_precompiled_dir}/multiview/${glsl_file}.spv)
                configure_file(${precompiled_file} ${out_file2} COPYONLY)
                
                if (glsl_stage STREQUAL "frag")
                    set(precompiled_file ${glsl_precompiled_dir}/fovDecode/${glsl_file}.spv)
                    configure_file(${precompiled_file} ${out_file3} COPYONLY)
                
                    set(precompiled_file ${glsl_precompiled_dir}/multiview/fovDecode/${glsl_file}.spv)
                    configure_file(${precompiled_file} ${out_file4} COPYONLY)
                endif()
            endif()
            list(APPEND glsl_output_files ${out_file} ${out_file2} ${out_file3} ${out_file4})
        endforeach()
        add_custom_target(${run_target_name} ALL DEPENDS ${glsl_output_files})
        set_target_properties(${run_target_name} PROPERTIES FOLDER ${HELPER_FOLDER})
    endfunction()
endif()

# Find dxc shader compiler, for d3d12 SM6.x shaders.
if (WIN32 AND MSVC)

    find_program(DXC_COMPILER dxc.exe)
    if(NOT (DXC_COMPILER STREQUAL "DXC_COMPILER-NOTFOUND"))
        message(NOTICE "Found dxc: ${DXC_COMPILER}")
    else()
        message(NOTICE "Could NOT find dxc, using precompiled CSO files")
    endif()
    
    function(compile_hlsl_sm6 run_target_name)
        set(DXC_FLAGS -nologo -WX -Ges -Zi -Zpc -Qstrip_reflect -Qstrip_debug -O3 -DENABLE_SM6_MULTI_VIEW)
        set(SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders/multiview)

        set(cso_output_files "")
        foreach(in_file IN LISTS ARGN)
            get_filename_component(hlsl_file ${in_file} NAME_WE)            
            string(REGEX REPLACE "([A-Za-z0-9]*)[_]" "" shader_stage ${hlsl_file})
            
            if(shader_stage STREQUAL "vert")
                set(vs_out_file ${SHADER_DIR}/${hlsl_file}.cso)
            elseif(shader_stage STREQUAL "frag")
                set(ps_out_file  ${SHADER_DIR}/${hlsl_file}.cso)
                set(ps_out_file2 ${SHADER_DIR}/yuv3PlaneFmt/${hlsl_file}.cso)
                set(ps_out_file3 ${SHADER_DIR}/fovDecode/${hlsl_file}.cso)
                set(ps_out_file4 ${SHADER_DIR}/fovDecode/yuv3PlaneFmt/${hlsl_file}.cso)
            else()
                set(vs_out_file ${SHADER_DIR}/${hlsl_file}_vert.cso)
                set(ps_out_file ${SHADER_DIR}/${hlsl_file}_frag.cso)
            endif()

            if(NOT (DXC_COMPILER STREQUAL "DXC_COMPILER-NOTFOUND"))
                if(shader_stage STREQUAL "vert")
                    add_custom_command(
                        OUTPUT ${vs_out_file} COMMAND ${DXC_COMPILER} ${DXC_FLAGS} -T vs_6_1 -E MainVS ${in_file} -Fo ${vs_out_file}
                        DEPENDS ${in_file}
                        VERBATIM
                    )
                elseif(shader_stage STREQUAL "frag")
                    add_custom_command(
                        OUTPUT ${ps_out_file}  COMMAND ${DXC_COMPILER} ${DXC_FLAGS} -T ps_6_1 -E MainPS ${in_file} -Fo ${ps_out_file}
                        OUTPUT ${ps_out_file2} COMMAND ${DXC_COMPILER} ${DXC_FLAGS} -D ENABLE_3PLANE_FMT_SAMPLER -T ps_6_1 -E MainPS ${in_file} -Fo ${ps_out_file2}
                        OUTPUT ${ps_out_file3} COMMAND ${DXC_COMPILER} ${DXC_FLAGS} -D ENABLE_FOVEATION_DECODE -T ps_6_1 -E MainPS ${in_file} -Fo ${ps_out_file3}
                        OUTPUT ${ps_out_file4} COMMAND ${DXC_COMPILER} ${DXC_FLAGS} -D ENABLE_3PLANE_FMT_SAMPLER -D ENABLE_FOVEATION_DECODE -T ps_6_1 -E MainPS ${in_file} -Fo ${ps_out_file4}
                        DEPENDS ${in_file}
                        VERBATIM
                    )
                else() # No shader stage specific file suffix, compile all supported stages.
                    add_custom_command(
                        OUTPUT ${vs_out_file} COMMAND ${DXC_COMPILER} ${DXC_FLAGS} -T vs_6_1 -E MainVS ${in_file} -Fo ${vs_out_file}
                        OUTPUT ${ps_out_file} COMMAND ${DXC_COMPILER} ${DXC_FLAGS} -T ps_6_1 -E MainPS ${in_file} -Fo ${ps_out_file}
                        DEPENDS ${in_file}
                        VERBATIM
                    )
                endif()
            else()
                # Use the precompiled .cso files
                get_filename_component(hlsl_src_dir ${in_file} DIRECTORY)
                set(hlsl_precompiled_dir ${hlsl_src_dir}/precompiled/multiview)

                if(shader_stage STREQUAL "vert")
                    set(precompiled_file ${hlsl_precompiled_dir}/${hlsl_file}.cso)
                    configure_file(${precompiled_file} ${vs_out_file} COPYONLY)
                elseif(shader_stage STREQUAL "frag")
                    set(precompiled_file ${hlsl_precompiled_dir}/${hlsl_file}.cso)
                    configure_file(${precompiled_file} ${ps_out_file} COPYONLY)

                    set(precompiled_file ${hlsl_precompiled_dir}/yuv3PlaneFmt/${hlsl_file}.cso)
                    configure_file(${precompiled_file} ${ps_out_file2} COPYONLY)

                    set(precompiled_file ${hlsl_precompiled_dir}/fovDecode/${hlsl_file}.cso)
                    configure_file(${precompiled_file} ${ps_out_file3} COPYONLY)
                
                    set(precompiled_file ${hlsl_precompiled_dir}/fovDecode/yuv3PlaneFmt/${hlsl_file}.cso)
                    configure_file(${precompiled_file} ${ps_out_file4} COPYONLY)
                else() # No shader stage specific file suffix, compile all supported stages.
                    set(precompiled_file ${hlsl_precompiled_dir}/${hlsl_file}_vert.cso)
                    configure_file(${precompiled_file} ${vs_out_file} COPYONLY)
                    set(precompiled_file ${hlsl_precompiled_dir}/${hlsl_file}_frag.cso)
                    configure_file(${precompiled_file} ${ps_out_file} COPYONLY)
                endif()
            endif()
            list(APPEND cso_output_files ${vs_out_file} ${ps_out_file} ${ps_out_file2} ${ps_out_file3} ${ps_out_file4})
        endforeach()
        add_custom_target(${run_target_name} ALL DEPENDS ${cso_output_files})
        set_target_properties(${run_target_name} PROPERTIES FOLDER ${HELPER_FOLDER})
    endfunction()

    find_program(FXC_COMPILER fxc.exe)
    if(NOT (FXC_COMPILER STREQUAL "FXC_COMPILER-NOTFOUND"))
        message(NOTICE "Found fxc: ${FXC_COMPILER}")
    else()
        message(NOTICE "Could NOT find fxc, using precompiled CSO SM5 files")
    endif()

    function(compile_hlsl_sm5 run_target_name)
        set(FXC_FLAGS /nologo /WX /Ges /Zi /Zpc /Qstrip_reflect /Qstrip_debug /O3)
        set(cso_output_files "")
        
        set(BASE_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
        set(SHADER_DIR_LIST SM5 SM5/multivew)
        set(VIEW_DEF_LIST "" /DENABLE_SM5_MULTI_VIEW)

        foreach(in_file IN LISTS ARGN)
            get_filename_component(hlsl_file ${in_file} NAME_WE)            
            string(REGEX REPLACE "([A-Za-z0-9]*)[_]" "" shader_stage ${hlsl_file})
            foreach(SHADER_SUBDIR VIEW_DEF IN ZIP_LISTS SHADER_DIR_LIST VIEW_DEF_LIST)                
                set(SHADER_DIR ${BASE_SHADER_DIR}/${SHADER_SUBDIR})
                
                if(shader_stage STREQUAL "vert")
                    set(vs_out_file ${SHADER_DIR}/${hlsl_file}.cso)
                elseif(shader_stage STREQUAL "frag")
                    set(ps_out_file  ${SHADER_DIR}/${hlsl_file}.cso)
                    set(ps_out_file2 ${SHADER_DIR}/yuv3PlaneFmt/${hlsl_file}.cso)
                    set(ps_out_file3 ${SHADER_DIR}/fovDecode/${hlsl_file}.cso)
                    set(ps_out_file4 ${SHADER_DIR}/fovDecode/yuv3PlaneFmt/${hlsl_file}.cso)
                else()
                    set(vs_out_file ${SHADER_DIR}/${hlsl_file}_vert.cso)
                    set(ps_out_file ${SHADER_DIR}/${hlsl_file}_frag.cso)
                endif()

                if(NOT (FXC_COMPILER STREQUAL "FXC_COMPILER-NOTFOUND"))
                    if(shader_stage STREQUAL "vert")
                        add_custom_command(
                            OUTPUT ${vs_out_file} COMMAND ${FXC_COMPILER} ${FXC_FLAGS} ${VIEW_DEF} /Tvs_5_0 /EMainVS ${in_file} /Fo ${vs_out_file}
                            DEPENDS ${in_file}
                            VERBATIM
                        )
                    elseif(shader_stage STREQUAL "frag")
                        add_custom_command(
                            OUTPUT ${ps_out_file}  COMMAND ${FXC_COMPILER} ${FXC_FLAGS} ${VIEW_DEF} /Tps_5_0 /EMainPS ${in_file} /Fo ${ps_out_file}
                            OUTPUT ${ps_out_file2} COMMAND ${FXC_COMPILER} ${FXC_FLAGS} ${VIEW_DEF} /DENABLE_3PLANE_FMT_SAMPLER /Tps_5_0 /EMainPS ${in_file} /Fo ${ps_out_file2}
                            OUTPUT ${ps_out_file3} COMMAND ${FXC_COMPILER} ${FXC_FLAGS} ${VIEW_DEF} /DENABLE_FOVEATION_DECODE /Tps_5_0 /EMainPS ${in_file} /Fo ${ps_out_file3}
                            OUTPUT ${ps_out_file4} COMMAND ${FXC_COMPILER} ${FXC_FLAGS} ${VIEW_DEF} /DENABLE_3PLANE_FMT_SAMPLER /DENABLE_FOVEATION_DECODE /Tps_5_0 /EMainPS ${in_file} /Fo ${ps_out_file4}
                            DEPENDS ${in_file}
                            VERBATIM
                        )
                    else() # No shader stage specific file suffix, compile all supported stages.
                        add_custom_command(
                            OUTPUT ${vs_out_file} COMMAND ${FXC_COMPILER} ${FXC_FLAGS} ${VIEW_DEF} /Tvs_5_0 /EMainVS ${in_file} /Fo ${vs_out_file}
                            OUTPUT ${ps_out_file} COMMAND ${FXC_COMPILER} ${FXC_FLAGS} ${VIEW_DEF} /Tps_5_0 /EMainPS ${in_file} /Fo ${ps_out_file}
                            DEPENDS ${in_file}
                            VERBATIM
                        )
                    endif()
                else()
                    # Use the precompiled .cso files
                    get_filename_component(hlsl_src_dir ${in_file} DIRECTORY)
                    set(hlsl_precompiled_dir ${hlsl_src_dir}/precompiled/${SHADER_SUBDIR})

                    if(shader_stage STREQUAL "vert")
                        set(precompiled_file ${hlsl_precompiled_dir}/${hlsl_file}.cso)
                        configure_file(${precompiled_file} ${vs_out_file} COPYONLY)
                    elseif(shader_stage STREQUAL "frag")
                        set(precompiled_file ${hlsl_precompiled_dir}/${hlsl_file}.cso)
                        configure_file(${precompiled_file} ${ps_out_file} COPYONLY)

                        set(precompiled_file ${hlsl_precompiled_dir}/yuv3PlaneFmt/${hlsl_file}.cso)
                        configure_file(${precompiled_file} ${ps_out_file2} COPYONLY)

                        set(precompiled_file ${hlsl_precompiled_dir}/fovDecode/${hlsl_file}.cso)
                        configure_file(${precompiled_file} ${ps_out_file3} COPYONLY)
                
                        set(precompiled_file ${hlsl_precompiled_dir}/fovDecode/yuv3PlaneFmt/${hlsl_file}.cso)
                        configure_file(${precompiled_file} ${ps_out_file4} COPYONLY)
                    else() # No shader stage specific file suffix, compile all supported stages.
                        set(precompiled_file ${hlsl_precompiled_dir}/${hlsl_file}_vert.cso)
                        configure_file(${precompiled_file} ${vs_out_file} COPYONLY)
                        set(precompiled_file ${hlsl_precompiled_dir}/${hlsl_file}_frag.cso)
                        configure_file(${precompiled_file} ${ps_out_file} COPYONLY)
                    endif()
                endif()
                list(APPEND cso_output_files ${vs_out_file} ${ps_out_file} ${ps_out_file2} ${ps_out_file3} ${ps_out_file4})
            endforeach()
        endforeach()
        add_custom_target(${run_target_name} ALL DEPENDS ${cso_output_files})
        set_target_properties(${run_target_name} PROPERTIES FOLDER ${HELPER_FOLDER})
    endfunction()

else()
    function(compile_hlsl_sm6 run_target_name)
        set(cso_output_files "")
        add_custom_target(${run_target_name} ALL DEPENDS ${cso_output_files})
        set_target_properties(${run_target_name} PROPERTIES FOLDER ${HELPER_FOLDER})
    endfunction()
    function(compile_hlsl_sm5 run_target_name)
        set(cso_output_files "")
        add_custom_target(${run_target_name} ALL DEPENDS ${cso_output_files})
        set_target_properties(${run_target_name} PROPERTIES FOLDER ${HELPER_FOLDER})
    endfunction()
endif()

# Not available in MinGW
if(MSVC)
    set(XR_USE_GRAPHICS_API_D3D11 TRUE)
    add_definitions(-DXR_USE_GRAPHICS_API_D3D11)
    set(XR_USE_GRAPHICS_API_D3D12 TRUE)
    add_definitions(-DXR_USE_GRAPHICS_API_D3D12)
endif()

# Check for the existence of the secure_getenv or __secure_getenv commands
include(CheckFunctionExists)

check_function_exists(secure_getenv HAVE_SECURE_GETENV)
check_function_exists(__secure_getenv HAVE___SECURE_GETENV)
configure_file(common_config.h.in "${CMAKE_CURRENT_BINARY_DIR}/common_config.h")
add_definitions(-DOPENXR_HAVE_COMMON_CONFIG)

if(NOT ANDROID)
    include(CheckSymbolExists)
     # This symbol check does not work with ndk, even with force c11 required or setting CMAKE_REQUIRED_FLAGS as per cmake docs.
    check_symbol_exists(timespec_get time.h HAVE_TIMESPEC_GET)
else()
    set(HAVE_TIMESPEC_GET TRUE)
endif()
if(HAVE_TIMESPEC_GET)
    add_definitions(-DXR_USE_TIMESPEC)
endif()

# Set up the OpenXR version variables, used by several targets in this project.
include(${CMAKE_CURRENT_SOURCE_DIR}/version.cmake)

# General code generation macro used by several targets.
macro(run_xr_xml_generate dependency output)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${output}"
       AND NOT BUILD_FORCE_GENERATION
    )
        # pre-generated found
        message(
            STATUS "Found and will use pre-generated ${output} in source tree"
        )
        list(APPEND GENERATED_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/${output}")
    else()
        if(NOT Python3_EXECUTABLE)
            message(
                FATAL_ERROR
                    "Python 3 not found, but pre-generated ${CMAKE_CURRENT_SOURCE_DIR}/${output} not found"
            )
        endif()
        add_custom_command(
            OUTPUT "${output}"
            COMMAND
                "${CMAKE_COMMAND}" -E env "PYTHONPATH=${CODEGEN_PYTHON_PATH}"
                "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/src/scripts/src_genxr.py" -registry
                "${PROJECT_SOURCE_DIR}/specification/registry/xr.xml"
                "${output}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            DEPENDS
                "${PROJECT_SOURCE_DIR}/specification/registry/xr.xml"
                "${PROJECT_SOURCE_DIR}/specification/scripts/generator.py"
                "${PROJECT_SOURCE_DIR}/specification/scripts/reg.py"
                "${PROJECT_SOURCE_DIR}/src/scripts/${dependency}"
                "${PROJECT_SOURCE_DIR}/src/scripts/src_genxr.py"
                ${ARGN}
            VERBATIM
            COMMENT
                "Generating ${output} using ${Python3_EXECUTABLE} on ${dependency}"
        )
        set_source_files_properties(${output} PROPERTIES GENERATED TRUE)
        list(APPEND GENERATED_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${output}")
        list(APPEND GENERATED_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${output}")
    endif()
endmacro()

# Layer JSON generation macro used by several targets.
macro(
    gen_xr_layer_json
    filename
    layername
    libfile
    version
    desc
    genbad
)
    add_custom_command(
        OUTPUT "${filename}"
        COMMAND
            "${CMAKE_COMMAND}" -E env "PYTHONPATH=${CODEGEN_PYTHON_PATH}"
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/src/scripts/generate_api_layer_manifest.py"
            -f "${filename}" -n ${layername} -l ${libfile} -a ${MAJOR}.${MINOR}
            -v ${version} ${genbad} -d ${desc}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS
            "${PROJECT_SOURCE_DIR}/src/scripts/generate_api_layer_manifest.py"
        COMMENT
            "Generating API Layer JSON ${filename} using -f ${filename} -n ${layername} -l ${libfile} -a ${MAJOR}.${MINOR} -v ${version} ${genbad} -d ${desc}"
        VERBATIM
    )
endmacro()

# Custom target for generated dispatch table sources, used by several targets.
unset(GENERATED_OUTPUT)
unset(GENERATED_DEPENDS)
run_xr_xml_generate(utility_source_generator.py xr_generated_dispatch_table.h)
run_xr_xml_generate(utility_source_generator.py xr_generated_dispatch_table.c)
set(COMMON_GENERATED_OUTPUT ${GENERATED_OUTPUT})
set(COMMON_GENERATED_DEPENDS ${GENERATED_DEPENDS})

if(COMMON_GENERATED_DEPENDS)
    add_custom_target(
        xr_common_generated_files DEPENDS ${COMMON_GENERATED_DEPENDS}
    )
else()
    add_custom_target(xr_common_generated_files)
endif()

set_target_properties(
    xr_common_generated_files PROPERTIES FOLDER ${CODEGEN_FOLDER}
)

unset(GENERATED_OUTPUT)
unset(GENERATED_DEPENDS)
run_xr_xml_generate(
    utility_source_generator.py xr_generated_dispatch_table_core.h
)
run_xr_xml_generate(
    utility_source_generator.py xr_generated_dispatch_table_core.c
)
set(LOADER_GENERATED_OUTPUT ${GENERATED_OUTPUT})
set(LOADER_GENERATED_DEPENDS ${GENERATED_DEPENDS})

unset(GENERATED_OUTPUT)
unset(GENERATED_DEPENDS)

if(LOADER_GENERATED_DEPENDS)
    add_custom_target(
        xr_global_generated_files DEPENDS ${LOADER_GENERATED_DEPENDS}
    )
else()
    add_custom_target(xr_global_generated_files)
endif()

set_target_properties(
    xr_global_generated_files PROPERTIES FOLDER ${CODEGEN_FOLDER}
)

if(NOT MSVC)
    include(CheckCXXCompilerFlag)
    include(CheckCCompilerFlag)
    foreach(
        FLAG
        -Wall
        -Werror=unused-parameter
        -Werror=unused-argument
        -Wpointer-arith
    )
        string(
            REGEX
            REPLACE
                "[^A-Za-z0-9]"
                ""
                _flagvar
                "${FLAG}"
        )
        check_cxx_compiler_flag(${FLAG} SUPPORTS_${_flagvar})
        if(SUPPORTS_${_flagvar})
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
        endif()
        check_c_compiler_flag(${FLAG} SUPPORTS_C_${_flagvar})
        if(SUPPORTS_C_${_flagvar})
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
        endif()
    endforeach()
    if(APPLE)
        set(CMAKE_SHARED_LINKER_FLAGS
            "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-undefined,error"
        )
    elseif(NOT WIN32)
        set(CMAKE_SHARED_LINKER_FLAGS
            "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined"
        )
    endif()
endif()

if(ANDROID)
    find_library(ANDROID_LIBRARY NAMES android)
    find_library(ANDROID_LOG_LIBRARY NAMES log)
endif()

set(OCULUS_OPENXR_MOBILE_SDK ${PROJECT_SOURCE_DIR}/src/external/oculus/OpenXR)

if (USE_PICO_MOBILE_LOADER AND ANDROID)
    message(NOTICE "Using Pico 4/Neo 3 Mobile OpenXR (PUI >= 5.2.x) Loader.")
    set(PICO_OPENXR_MOBILE_SDK ${PROJECT_SOURCE_DIR}/src/external/pico)
    add_library(openxr_loader SHARED IMPORTED)
    set_property(
            TARGET
            openxr_loader
            PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES
            ${PICO_OPENXR_MOBILE_SDK}/include
    )
    set_property(
            TARGET
            openxr_loader
            PROPERTY
            IMPORTED_LOCATION
            "${PICO_OPENXR_MOBILE_SDK}/libs/android.${ANDROID_ABI}/libopenxr_loader.so"
    )
    add_definitions(-DXR_USE_OXR_PICO)
    install(IMPORTED_RUNTIME_ARTIFACTS openxr_loader
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
else()
    if(BUILD_LOADER)
        add_subdirectory(loader)
    endif()
endif()

if(BUILD_API_LAYERS)
    add_subdirectory(api_layers)
endif()

add_subdirectory(alxr_engine)

if(BUILD_TESTS OR BUILD_CONFORMANCE_TESTS)
    add_subdirectory(external/catch2)
endif()

if(BUILD_CONFORMANCE_TESTS)
    add_subdirectory(conformance)
endif()
