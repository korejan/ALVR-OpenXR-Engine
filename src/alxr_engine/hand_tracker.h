#pragma once
#ifndef ALXR_XR_HAND_TRACKER_H
#define ALXR_XR_HAND_TRACKER_H
#include "pch.h"
#include <array>
#include "alxr_ctypes.h"
#include "xr_context.h"
#include "xr_eigen.h"

struct ALXRHandTracking;

namespace ALXR {

struct HandTrackingState final {
	XrHandTrackingAimFlagsFB    aimStatus {0};
	XrHandTrackingDataSourceEXT dataSource{XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT};
};

struct XrHandTracker final {
	XrHandTracker(const XrContext& ctx);
	~XrHandTracker();

	bool IsSupported() const;
	bool IsEnabled() const;

	using TrackingStateList = std::array<HandTrackingState, 2>;

	bool GetJointLocations(const XrTime& time, const XrSpace space,
                           ALXRHandTracking& handTrackingData,
                           TrackingStateList* trackingStates = nullptr) const;

	bool GetJointLocations(const XrTime& time, const XrSpace space,
                           ALXRTrackingInfo::Controller (&controllerInfo)[2]) /*const*/;

private:
	bool LoadExtFunctions();
	XrHandTrackingDataSourceEXT GetDataSource(const XrHandJointLocationsEXT& jointLocations) const;
	XrHandTrackingAimFlagsFB    GetAimStatus(const XrHandJointLocationsEXT& jointLocations) const;
	
	XrContext m_ctx;
	const XrRuntimeType m_runtimeType{ XrRuntimeType::Unknown };
	const bool m_isEXTHandTrackingDataSourceSupported{ false };
	const bool m_isFBHandTrackingAimSupported{ false };

	struct HandTrackerData final
	{
		std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> jointLocations;
		//std::array<XrHandJointVelocityEXT, XR_HAND_JOINT_COUNT_EXT> jointVelocities;
		Eigen::Quaternionf baseOrientation{ Eigen::Quaternionf::Identity() };
		XrHandTrackerEXT tracker{ XR_NULL_HANDLE };
	};
	std::array<HandTrackerData, 2> m_handTrackers{};

#ifndef XR_EXTENSION_PROTOTYPES
	PFN_xrCreateHandTrackerEXT  xrCreateHandTrackerEXT  = nullptr;
	PFN_xrLocateHandJointsEXT   xrLocateHandJointsEXT   = nullptr;
	PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT = nullptr;
#endif
};

}
#endif
