#pragma once
#ifndef ALXR_XR_CONTEXT_H
#define ALXR_XR_CONTEXT_H
#include "pch.h"
#include <optional>
#include <tuple>
#include <string_view>

namespace ALXR {

enum class XrRuntimeType
{
	SteamVR,
	Monado,
	WMR,
	Oculus,
	Pico,
	HTCWave,
	MagicLeap,
	SnapdragonMonado,
    AndroidXR,
    VirtualDesktopXR,
	Unknown,
	////////////////////////
	TypeCount
};

struct XrContext {
	XrInstance  instance = XR_NULL_HANDLE;
	XrSession   session  = XR_NULL_HANDLE;

	XrSystemId    GetSystemId() const;
	XrRuntimeType GetRuntimeType() const;

	std::int64_t ToNanoseconds(const XrTime xrt) const;
	XrTime       ToXrTime(const std::int64_t timeNS) const;

	std::tuple<XrTime, std::int64_t> XrTimeNow() const;

	constexpr bool IsValid() const {
		return instance != XR_NULL_HANDLE && session != XR_NULL_HANDLE;
	}

	constexpr bool operator==(const XrContext& rhs) const {
		return instance == rhs.instance && session == rhs.session;
	}

	constexpr bool operator!=(const XrContext& rhs) const {
		return !(*this == rhs);
	}
};

constexpr inline const char* ToString(const XrRuntimeType t) {
	switch (t) {
	case XrRuntimeType::SteamVR:   return "SteamVR";
	case XrRuntimeType::Monado:    return "Monado";
	case XrRuntimeType::WMR:       return "Windows Mixed Reality";
	case XrRuntimeType::Oculus:    return "Oculus";
	case XrRuntimeType::Pico:      return "Pico";
	case XrRuntimeType::HTCWave:   return "VIVE WAVE";
	case XrRuntimeType::MagicLeap: return "MAGICLEAP";
	case XrRuntimeType::SnapdragonMonado: return "Snapdragon";
    case XrRuntimeType::AndroidXR: return "Android XR";
    case XrRuntimeType::VirtualDesktopXR: return "VirtualDesktopXR";
	default: return "Unknown";
	}
}

constexpr inline XrRuntimeType FromString(const std::string_view runtimeName) {
    for (const auto& [name,rtType] : { 
        std::make_tuple("SteamVR", XrRuntimeType::SteamVR),
        { "Monado", XrRuntimeType::Monado },
        { "Windows Mixed Reality", XrRuntimeType::WMR },
        { "Oculus", XrRuntimeType::Oculus },
        { "Pico", XrRuntimeType::Pico },
        { "VIVE WAVE", XrRuntimeType::HTCWave },
        { "MAGICLEAP", XrRuntimeType::MagicLeap },
        { "Snapdragon", XrRuntimeType::SnapdragonMonado },
        { "Android XR", XrRuntimeType::AndroidXR },
        { "Moohan", XrRuntimeType::AndroidXR },
        { "VirtualDesktopXR", XrRuntimeType::VirtualDesktopXR },
    }) {
        if (runtimeName.starts_with(name)) {
            return rtType;
        }
    }
    return XrRuntimeType::Unknown;
}

// TODO: Make a proper dispatch table for all core & ext funs...
inline struct XrExtFunctions final {
	constexpr inline XrExtFunctions() noexcept = default;
#ifdef XR_USE_PLATFORM_WIN32
	// XR_KHR_win32_convert_performance_counter_time
	PFN_xrConvertTimeToWin32PerformanceCounterKHR pxrConvertTimeToWin32PerformanceCounterKHR = nullptr;
	PFN_xrConvertWin32PerformanceCounterToTimeKHR pxrConvertWin32PerformanceCounterToTimeKHR = nullptr;
#endif
	// XR_KHR_convert_timespec_time
	PFN_xrConvertTimespecTimeToTimeKHR pxrConvertTimespecTimeToTimeKHR = nullptr;
	PFN_xrConvertTimeToTimespecTimeKHR pxrConvertTimeToTimespecTimeKHR = nullptr;
} gExtFns{};

}
#endif
