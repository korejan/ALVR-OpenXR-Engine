// Copyright (c) 2017-2025 The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <time.h>
#include <string.h>

//
// Platform Headers
//
#ifdef XR_USE_PLATFORM_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif  // !WIN32_LEAN_AND_MEAN

#ifndef NOMINMAX
#define NOMINMAX
#endif  // !NOMINMAX

#include <windows.h>
#include <wrl/client.h>  // For Microsoft::WRL::ComPtr
#endif

#ifdef XR_USE_PLATFORM_ANDROID
#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <jni.h>
#include <sys/system_properties.h>
#endif

#ifdef XR_USE_PLATFORM_WAYLAND
#include "wayland-client.h"
#endif

#ifdef XR_USE_PLATFORM_XLIB
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#ifdef XR_USE_PLATFORM_XCB
#include <xcb/xcb.h>
#endif

//
// Graphics Headers
//
#ifdef XR_USE_GRAPHICS_API_D3D11
#include <dxgi1_6.h>
#include <d3d11_4.h>
#endif

#ifdef XR_USE_GRAPHICS_API_D3D12
#include <d3d12.h>
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL
#if defined(XR_USE_PLATFORM_XLIB) || defined(XR_USE_PLATFORM_XCB)
#include <glad/glx.h>
#endif
#ifdef XR_USE_PLATFORM_XCB
#include <xcb/glx.h>
#endif
#ifdef XR_USE_PLATFORM_WIN32
#include <wingdi.h>  // For HGLRC
#endif
#include <glad/gl.h>
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
#include <glad/egl.h>
#endif

#ifdef XR_USE_PLATFORM_EGL
#include <glad/egl.h>
#endif  // XR_USE_PLATFORM_EGL

#ifdef XR_USE_GRAPHICS_API_VULKAN
#ifdef XR_USE_PLATFORM_WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#ifdef XR_USE_PLATFORM_ANDROID
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#endif

//
// OpenXR Headers
//
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>
#include "android_avatar_eyes.h"

#ifdef XR_USE_OXR_PICO
    // These are no longer declared/defined in the Pico OpenXR SDK v2.1.0.
	#ifndef XR_PICO_BOUNDARY_EXT_EXTENSION_NAME
		#define XR_PICO_BOUNDARY_EXT_EXTENSION_NAME "XR_PICO_boundary_ext"
	#endif
    typedef enum xrFuncitonName {
        XR_SET_SEETHROUGH_BACKGROUND = 15,
    } xrFuncitonName;
    typedef XrResult(XRAPI_PTR* PFN_xrInvokeFunctionsPICO)(
        XrSession                                 session,
        xrFuncitonName                            name,
        void* input,
        unsigned int                              size_in,
        void** output,
        unsigned int                              size_out);
#endif
