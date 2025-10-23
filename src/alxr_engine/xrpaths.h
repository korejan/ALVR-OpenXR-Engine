#pragma once
#ifndef ALXR_ENGINE_XRPATHS_H
#define ALXR_ENGINE_XRPATHS_H

namespace ALXR::XRPaths {
	constexpr inline const char* const UserHandLeft	    = "/user/hand/left";
	constexpr inline const char* const UserHandRight    = "/user/hand/right";
	constexpr inline const char* const UserHandLeftHTC  = "/user/hand_htc/left";
	constexpr inline const char* const UserHandRightHTC = "/user/hand_htc/right";
	constexpr inline const char* const UserEyesExt		= "/user/eyes_ext";

	constexpr inline const char* const SelectClick = "select/click";
	constexpr inline const char* const SelectValue = "select/value";
	constexpr inline const char* const SqueezeValue = "squeeze/value";
	constexpr inline const char* const SqueezeForce = "squeeze/force";
	constexpr inline const char* const SqueezeClick = "squeeze/click";
	constexpr inline const char* const SqueezeTouch = "squeeze/touch";
	constexpr inline const char* const GripPose = "grip/pose";
	constexpr inline const char* const AimPose = "aim/pose";
	constexpr inline const char* const Haptic = "haptic";
	constexpr inline const char* const SystemClick = "system/click";
	constexpr inline const char* const MenuClick = "menu/click";
	constexpr inline const char* const BackClick = "back/click";
	constexpr inline const char* const AClick = "a/click";
	constexpr inline const char* const ATouch = "a/touch";
	constexpr inline const char* const BClick = "b/click";
	constexpr inline const char* const BTouch = "b/touch";
	constexpr inline const char* const XClick = "x/click";
	constexpr inline const char* const XTouch = "x/touch";
	constexpr inline const char* const YClick = "y/click";
	constexpr inline const char* const YTouch = "y/touch";
	constexpr inline const char* const TriggerClick = "trigger/click";
	constexpr inline const char* const TriggerTouch = "trigger/touch";
	constexpr inline const char* const TriggerValue = "trigger/value";
	constexpr inline const char* const ThumbstickPos = "thumbstick";
	constexpr inline const char* const ThumbstickX = "thumbstick/x";
	constexpr inline const char* const ThumbstickY = "thumbstick/y";
	constexpr inline const char* const ThumbstickClick = "thumbstick/click";
	constexpr inline const char* const ThumbstickTouch = "thumbstick/touch";
	constexpr inline const char* const ThumbrestTouch = "thumbrest/touch";
	constexpr inline const char* const TrackpadPos = "trackpad";
	constexpr inline const char* const TrackpadX = "trackpad/x";
	constexpr inline const char* const TrackpadY = "trackpad/y";
	constexpr inline const char* const TrackpadClick = "trackpad/click";
	constexpr inline const char* const TrackpadTouch = "trackpad/touch";
	constexpr inline const char* const TrackpadForce = "trackpad/force";
	constexpr inline const char* const ShoulderClick = "shoulder/click";
	constexpr inline const char* const GazeExtPose = "gaze_ext/pose";
	constexpr inline const char* const PrimaryClick = "primary/click";
	constexpr inline const char* const SecondaryClick = "secondary/click";
}
#endif
