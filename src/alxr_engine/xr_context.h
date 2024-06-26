#pragma once
#ifndef ALXR_XR_CONTEXT_H
#define ALXR_XR_CONTEXT_H
#include "pch.h"
#include <optional>
#include <tuple>
#include <string_view>
#include <unordered_map>

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
	Unknown,
	////////////////////////
	TypeCount
};

using XrExtensionMap = std::unordered_map<std::string_view, bool>;

struct XrContext {
	XrInstance instance = XR_NULL_HANDLE;
	XrSession  session  = XR_NULL_HANDLE;
	const XrExtensionMap* extensions = nullptr;

	XrSystemId    GetSystemId() const;
	XrRuntimeType GetRuntimeType() const;

	std::int64_t ToNanoseconds(const XrTime xrt) const;
	XrTime       ToXrTime(const std::int64_t timeNS) const;

	std::tuple<XrTime, std::int64_t> XrTimeNow() const;

	constexpr bool IsValid() const {
		return instance != XR_NULL_HANDLE && session != XR_NULL_HANDLE;
	}

	bool IsExtEnabled(const std::string_view& extName) const {
		if (extensions == nullptr || !IsValid())
			return false;
		const auto ext_itr = extensions->find(extName);
		return ext_itr != extensions->end() && ext_itr->second;
	}

	constexpr bool operator==(const XrContext& rhs) const {
		return instance == rhs.instance && session == rhs.session;
	}

	constexpr bool operator!=(const XrContext& rhs) const {
		return !(*this == rhs);
	}
};


template <typename XrTargetT, typename XrSourceInT>
constexpr inline const XrTargetT* GetChained(const XrSourceInT& val, const XrStructureType stuctType) {
	for (auto curr = reinterpret_cast<const XrBaseInStructure*>(val.next);
		curr != nullptr; curr = curr->next) {
		if (curr->type == stuctType) {
			return reinterpret_cast<const XrTargetT*>(curr);
		}
	}
	return nullptr;
}

template <typename XrTargetT, typename XrSourceOutT>
constexpr inline XrTargetT* GetChained(XrSourceOutT& val, const XrStructureType stuctType) {
	for (auto curr = reinterpret_cast<XrBaseOutStructure*>(val.next);
		curr != nullptr; curr = curr->next) {
		if (curr->type == stuctType) {
			return reinterpret_cast<XrTargetT*>(curr);
		}
	}
	return nullptr;
}

constexpr inline std::string_view ToString(const XrRuntimeType t) {
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
	default: return "Unknown";
	}
}

constexpr inline XrRuntimeType FromString(const std::string_view runtimeName) {
	for (std::size_t idx = 0; idx < std::size_t(XrRuntimeType::TypeCount); ++idx) {
		const std::string_view namePrefix = ToString(XrRuntimeType(idx));
		if (runtimeName.starts_with(namePrefix))
			return static_cast<XrRuntimeType>(idx);
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
