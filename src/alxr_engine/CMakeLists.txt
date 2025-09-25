# Copyright (c) 2017 The Khronos Group Inc.
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
# Author:
#
include(FetchContent)

FetchContent_Declare(
  readerwriterqueue
  GIT_REPOSITORY    https://github.com/cameron314/readerwriterqueue
  GIT_TAG           master
)
FetchContent_MakeAvailable(readerwriterqueue)

FetchContent_Declare(
  Eigen
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG 5.0.0
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)
set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_BTL OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_SPBENC OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_BLAS OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_LAPACK OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(Eigen)
if(NOT eigen_POPULATED)
  FetchContent_Populate(Eigen)
  add_subdirectory(${eigen_SOURCE_DIR} ${eigen_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

file(GLOB LOCAL_HEADERS "*.h")
file(GLOB LOCAL_SOURCE "*.cpp")
file(GLOB VULKAN_SHADERS "vulkan_shaders/*.glsl")
file(GLOB D3D_SHADERS "d3d_shaders/*.hlsl")

# Mark HLSL files to prevent the VS generator adding them for automatic shader compilation with FXC (they should be compiled with DXC for SM6 support)
foreach(hlsl_file IN LISTS D3D_SHADERS)
    set_source_files_properties(${hlsl_file} PROPERTIES VS_TOOL_OVERRIDE "None")
endforeach()

# For including compiled shaders
include_directories(${CMAKE_CURRENT_BINARY_DIR})

option(DISABLE_DECODER_SUPPORT "Disable decoder support and decoder dependencies" OFF)
if (DISABLE_DECODER_SUPPORT)
    message(WARNING "Option \"DISABLE_DECODER_SUPPORT\" is ON, decoder support & dependencies are disabled.")
endif()

set(CUDA_LIB_LIST)
set(ENABLE_CUDA_INTEROP FALSE)
if(NOT ANDROID)
    option(BUILD_CUDA_INTEROP "Enables explicit CUDA buffer interop" ON)
    if (BUILD_CUDA_INTEROP)
        find_package(CUDAToolkit)
        if (CUDAToolkit_FOUND)
            set(CUDA_LIB_LIST CUDA::cudart_static CUDA::cuda_driver)
            set(ENABLE_CUDA_INTEROP TRUE)
            message(STATUS "Enabling CUDA Buffer Interop")
        endif()
    endif()
endif()

if(ANDROID)
    add_library(alxr_engine MODULE
        ${LOCAL_SOURCE}
        ${LOCAL_HEADERS}
        ${VULKAN_SHADERS})
        #$<TARGET_OBJECTS:android_native_app_glue>)
    target_link_libraries(alxr_engine ${ANDROID_LIBRARY} ${ANDROID_LOG_LIBRARY} mediandk android_native_app_glue)
    if (NOT DISABLE_DECODER_SUPPORT)
        target_link_libraries(alxr_engine mediandk)
    endif()
else()
    set(CMAKE_INSTALL_RPATH $ORIGIN)
    add_library(alxr_engine SHARED
        ${LOCAL_SOURCE}
        ${LOCAL_HEADERS}
        ${VULKAN_SHADERS}
        ${D3D_SHADERS})
endif()
set_target_properties(alxr_engine PROPERTIES FOLDER ${SAMPLES_FOLDER})
target_compile_definitions(alxr_engine PRIVATE ALXR_CLIENT ASIO_STANDALONE)
if (XR_IS_UWP)
    target_compile_definitions(alxr_engine PRIVATE XR_USE_PLATFORM_UWP)
endif()
if (DISABLE_DECODER_SUPPORT)
    target_compile_definitions(alxr_engine PRIVATE XR_DISABLE_DECODER_THREAD)
endif()

source_group("Headers" FILES ${LOCAL_HEADERS})
source_group("Shaders" FILES ${VULKAN_SHADERS})

compile_glsl(run_alxr_engine_glsl_compiles ${VULKAN_SHADERS})
compile_hlsl_sm5(run_alxr_engine_hlsl_sm5_compiles ${D3D_SHADERS})
compile_hlsl_sm6(run_alxr_engine_hlsl_sm6_compiles ${D3D_SHADERS})

add_dependencies(alxr_engine
    generate_openxr_header
    run_alxr_engine_glsl_compiles
    run_alxr_engine_hlsl_sm5_compiles
    run_alxr_engine_hlsl_sm6_compiles
)

set(ALVR_ROOT_DIR ${PROJECT_SOURCE_DIR}/../../../../.. CACHE PATH "Root directory of ALVR source")
set(ALVR_COMMON_DIR ${ALVR_ROOT_DIR}/alvr/client/android/ALVR-common)
set(ALVR_OLD_CLIENT_DIR ${ALVR_ROOT_DIR}/alvr/client/android/app/src/main/cpp)

file(GLOB_RECURSE ALVR_COMMON_HEADERS ${ALVR_COMMON_DIR}/*.h)
file(GLOB_RECURSE ALVR_COMMON_SOURCE ${ALVR_COMMON_DIR}/*.c ${ALVR_COMMON_DIR}/*.cpp)
add_library(alvr_common
    ${ALVR_COMMON_HEADERS}
    ${ALVR_OLD_CLIENT_DIR}/fec.h
    ${ALVR_OLD_CLIENT_DIR}/latency_collector.h
    ${ALVR_COMMON_SOURCE}
    ${ALVR_OLD_CLIENT_DIR}/fec.cpp
    ${ALVR_OLD_CLIENT_DIR}/latency_collector.cpp)
target_include_directories(alvr_common PRIVATE
    ${ALVR_COMMON_DIR}
)
target_compile_definitions(alvr_common PRIVATE ALXR_CLIENT)
if (MSVC)
    target_compile_definitions(alvr_common PRIVATE _CRT_SECURE_NO_WARNINGS PRIVATE _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
    target_compile_options(alvr_common PRIVATE /Zc:wchar_t /Zc:forScope /W3 /Zi)
endif()

set(FFMPEG_LIBS)
if(DISABLE_DECODER_SUPPORT OR ANDROID)
    set(FFMPEG_LIBS)
    set(ONE_API_TTB_LIBS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
    find_package(PkgConfig REQUIRED)
    set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)

    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavutil
        libavcodec
        libavfilter
        libswscale
    )

    set(FFMPEG_LIBS PkgConfig::LIBAV)
    
    #pkg_check_modules(TBB REQUIRED IMPORTED_TARGET
    #    tbb
    #)
    #set(ONE_API_TTB_LIBS PkgConfig::TBB)
    set(ONE_API_TTB_LIBS)
elseif (WIN32)

    # ffmpeg v8
    set(AVUTIL_DLL     "avutil-60.dll")
    set(AVDEVICE_DLL   "avdevice-62.dll")
    set(SWRESAMPLE_DLL "swresample-6.dll")
    set(AVCODEC_DLL    "avcodec-62.dll")
    set(AVFORMAT_DLL   "avformat-62.dll")
    set(AVFILTER_DLL   "avfilter-11.dll")
    set(SWSCALE_DLL    "swscale-9.dll")

    if (XR_IS_UWP)       
        # ffmpeg v7
        set(AVUTIL_DLL     "avutil-59.dll")
        set(AVDEVICE_DLL   "avdevice-61.dll")
        set(SWRESAMPLE_DLL "swresample-5.dll")
        set(AVCODEC_DLL    "avcodec-61.dll")
        set(AVFORMAT_DLL   "avformat-61.dll")
        set(AVFILTER_DLL   "avfilter-10.dll")
        set(SWSCALE_DLL    "swscale-8.dll")

        set(FFMPEG_PLATFORM_DIR "uwp")
        string(TOLOWER ${XR_TARGET_CPU_ARCH} XR_TARGET_CPU_ARCH_LOWERED)
        set(FFMPEG_DIR ${PROJECT_SOURCE_DIR}/src/external/ffmpeg/${FFMPEG_PLATFORM_DIR}/${XR_TARGET_CPU_ARCH_LOWERED})  
    else()
        FetchContent_Declare(
            ffmpeg_binaries
            URL https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.0-latest-win64-gpl-shared-8.0.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(ffmpeg_binaries)
        set(FFMPEG_DIR ${ffmpeg_binaries_SOURCE_DIR})
    endif()
  
    set(FFMPEG_BIN_DIR ${FFMPEG_DIR}/bin)
    set(FFMPEG_LIB_DIR ${FFMPEG_DIR}/lib)

    function(add_ffmpeg_shared_lib libName importLib dllName)
        add_library(${libName} SHARED IMPORTED)
        set_target_properties(${libName} PROPERTIES
            IMPORTED_LOCATION ${FFMPEG_BIN_DIR}/${dllName}
            IMPORTED_IMPLIB ${FFMPEG_LIB_DIR}/${importLib}
	        INTERFACE_INCLUDE_DIRECTORIES ${FFMPEG_DIR}/include)
    endfunction()  

    add_ffmpeg_shared_lib("libavutil" "avutil.lib" ${AVUTIL_DLL})
    add_ffmpeg_shared_lib("libavdevice" "avdevice.lib" ${AVDEVICE_DLL})
    add_ffmpeg_shared_lib("libswresample" "swresample.lib" ${SWRESAMPLE_DLL})
    add_ffmpeg_shared_lib("libavcodec" "avcodec.lib" ${AVCODEC_DLL})
    add_ffmpeg_shared_lib("libavformat" "avformat.lib" ${AVFORMAT_DLL})
    add_ffmpeg_shared_lib("libavfilter" "avfilter.lib" ${AVFILTER_DLL})
    add_ffmpeg_shared_lib("libswscale" "swscale.lib" ${SWSCALE_DLL})
    
    # avutil, avresample, avcodec, avformat 
    set(FFMPEG_LIBS libavutil libswresample libavdevice libavcodec libavformat libavfilter libswscale)
    set(ONE_API_TTB_LIBS)
endif()


    
#set(OCULUS_OXR_EXT_HEADERS)
#if (USE_OCULUS_OXR_EXT_HEADERS AND NOT ANDROID)
#    message(STATUS "Oculus OpenXR Extension Headers Enabled")
    set(OCULUS_OXR_EXT_HEADERS ${OCULUS_OPENXR_MOBILE_SDK})
#    add_definitions(-DXR_USE_OXR_OCULUS)
#endif()

set(ANDROID_XR_EXT_HEADERS ${PROJECT_SOURCE_DIR}/src/external/android-xr)

target_include_directories(alxr_engine
    PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/common
    ${PROJECT_SOURCE_DIR}/src/external/asio/include

    # for OpenXR headers
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include
    ${OCULUS_OXR_EXT_HEADERS}
    ${ANDROID_XR_EXT_HEADERS}

    # for helper headers
    ${PROJECT_SOURCE_DIR}/external/include
    
    ${ALVR_COMMON_DIR}
    ${ALVR_COMMON_DIR}/../
    ${ALVR_COMMON_DIR}/../app/src/main/cpp
    ${ALVR_OLD_CLIENT_DIR}
)

if(GLSLANG_VALIDATOR AND NOT GLSLC_COMMAND)
    target_compile_definitions(alxr_engine PRIVATE USE_GLSLANGVALIDATOR)
endif()

if(ENABLE_CUDA_INTEROP)
    target_compile_definitions(alxr_engine PRIVATE XR_ENABLE_CUDA_INTEROP)
endif()

if(Vulkan_FOUND)
    target_include_directories(alxr_engine
        PRIVATE
        ${Vulkan_INCLUDE_DIRS}
    )
endif()

if (XR_IS_UWP)
    target_link_libraries(alxr_engine OneCore.lib WindowsApp.lib)
endif()
target_link_libraries(alxr_engine openxr_loader alvr_common readerwriterqueue Eigen3::Eigen ${CUDA_LIB_LIST} ${FFMPEG_LIBS} ${ONE_API_TTB_LIBS})

if(TARGET openxr-gfxwrapper)
    target_link_libraries(alxr_engine openxr-gfxwrapper)
endif()
if(WIN32)
    if(MSVC)
        target_compile_definitions(alxr_engine PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
        target_compile_options(alxr_engine PRIVATE /Zc:wchar_t /Zc:forScope /W3 /WX /Zi)
        target_compile_options(alxr_engine PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/Zi>")
        target_link_options(alxr_engine PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/DEBUG>")
        target_link_options(alxr_engine PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>")
        target_link_options(alxr_engine PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>")
    endif()
    target_link_libraries(alxr_engine ole32)
    if(MSVC)
        # Right now can't build this on MinGW because of directxcolors, directxmath, etc.
        target_link_libraries(alxr_engine dxguid d3d11 d3d12 d3dcompiler dxgi)
    endif()
endif()

if(Vulkan_LIBRARY)
    target_link_libraries(alxr_engine ${Vulkan_LIBRARY})
endif()

if(MSVC)
    string(TOLOWER ${CMAKE_BUILD_TYPE} BUILD_TYPE_LOWERED)
    if (${BUILD_TYPE_LOWERED} MATCHES ".*debug.*")
        message("Installing debug CRT depends")
        set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
        set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
     endif()

    include(InstallRequiredSystemLibraries)
     foreach(libav_var ${FFMPEG_LIBS})
        install(IMPORTED_RUNTIME_ARTIFACTS ${libav_var}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach()
endif()

install(TARGETS alxr_engine
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(WIN32)
    install(FILES $<TARGET_PDB_FILE:alxr_engine> DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders/ DESTINATION ${CMAKE_INSTALL_BINDIR}/shaders)
endif()
