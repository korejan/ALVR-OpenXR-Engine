#pragma once
#ifndef ALXR_XR_CONTEXT_H
#define ALXR_XR_CONTEXT_H
#include "pch.h"
#include <optional>
#include <tuple>

namespace ALXR {

struct XrContext {
	XrInstance  instance = XR_NULL_HANDLE;
	XrSession   session  = XR_NULL_HANDLE;

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
