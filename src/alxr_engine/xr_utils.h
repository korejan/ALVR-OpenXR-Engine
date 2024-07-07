#pragma once
#ifndef ALXR_XR_UTILS_H
#define ALXR_XR_UTILS_H

#include "pch.h"
#include <limits>
#include <optional>

namespace ALXR {;

using float_limits = std::numeric_limits<float>;
constexpr inline const XrPosef IdentityPose { {0,0,0,1},{0,0,0} };
constexpr inline const XrPosef ZeroPose { {0,0,0,0},{0,0,0} };
constexpr inline const XrPosef InfinityPose {
    {
        float_limits::infinity(),
        float_limits::infinity(),
        float_limits::infinity(),
        float_limits::infinity(),
    },
    {
        float_limits::infinity(),
        float_limits::infinity(),
        float_limits::infinity(),
    }
};

constexpr inline const XrView IdentityView{
    .type = XR_TYPE_VIEW,
    .next = nullptr,
    .pose = ALXR::IdentityPose,
    .fov = { 0,0,0,0 }
};

constexpr inline bool operator==(const XrPosef& lhs, const XrPosef& rhs) {
    return lhs.position.x == rhs.position.x &&
           lhs.position.y == rhs.position.y &&
           lhs.position.z == rhs.position.z &&
           lhs.orientation.x == rhs.orientation.x &&
           lhs.orientation.y == rhs.orientation.y &&
           lhs.orientation.z == rhs.orientation.z &&
           lhs.orientation.w == rhs.orientation.w;
}

constexpr inline bool operator!=(const XrPosef& lhs, const XrPosef& rhs) {
    return !(lhs == rhs);
}

struct SpaceLoc
{
    XrPosef pose = IdentityPose;
    XrVector3f linearVelocity = { 0,0,0 };
    XrVector3f angularVelocity = { 0,0,0 };

    constexpr inline bool is_zero() const     { return pose == ZeroPose; }
    constexpr inline bool is_infinity() const { return pose == InfinityPose; }
};

constexpr inline const SpaceLoc IdentitySpaceLoc = { IdentityPose, {0,0,0}, {0,0,0} };
constexpr inline const SpaceLoc ZeroSpaceLoc = { ZeroPose, {0,0,0}, {0,0,0} };
constexpr inline const SpaceLoc InfinitySpaceLoc = { InfinityPose, {0,0,0}, {0,0,0} };

inline std::optional<SpaceLoc> GetSpaceLocation
(
    const XrSpace& targetSpace,
    const XrSpace& baseSpace,
    const XrTime& time
)
{
    XrSpaceVelocity velocity{ XR_TYPE_SPACE_VELOCITY, nullptr };
    XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION, &velocity };
    if (XR_FAILED(xrLocateSpace(targetSpace, baseSpace, time, &spaceLocation)) ||
        spaceLocation.locationFlags == 0) {
		return std::nullopt;
	}
    
    SpaceLoc result{ IdentitySpaceLoc };
    const auto& pose = spaceLocation.pose;
    if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0)
        result.pose.position = pose.position;

    if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
        result.pose.orientation = pose.orientation;

    if ((velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0)
        result.linearVelocity = velocity.linearVelocity;

    if ((velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0)
        result.angularVelocity = velocity.angularVelocity;

    return result;
}

constexpr inline XrVector3f GetHandJointScale(const XrHandJointEXT jointType) {
    switch (jointType) {
    case XR_HAND_JOINT_WRIST_EXT:
        return { 0.007f,0.007f, 0.007f };

    case XR_HAND_JOINT_PALM_EXT:
        return { 0.0055f,0.0055f, 0.0055f };

    case XR_HAND_JOINT_THUMB_METACARPAL_EXT:
    case XR_HAND_JOINT_INDEX_METACARPAL_EXT:
    case XR_HAND_JOINT_MIDDLE_METACARPAL_EXT:
    case XR_HAND_JOINT_RING_METACARPAL_EXT:
    case XR_HAND_JOINT_LITTLE_METACARPAL_EXT:
        return { 0.0045f,0.0045f, 0.0045f };

    case XR_HAND_JOINT_THUMB_PROXIMAL_EXT:
    case XR_HAND_JOINT_INDEX_PROXIMAL_EXT:
    case XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT:
    case XR_HAND_JOINT_RING_PROXIMAL_EXT:
    case XR_HAND_JOINT_LITTLE_PROXIMAL_EXT:
        return { 0.0035f,0.0035f, 0.0035f };

    case XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT:
    case XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT:
    case XR_HAND_JOINT_RING_INTERMEDIATE_EXT:
    case XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT:
        return { 0.003f,0.003f, 0.003f };

    case XR_HAND_JOINT_INDEX_DISTAL_EXT:
    case XR_HAND_JOINT_MIDDLE_DISTAL_EXT:
    case XR_HAND_JOINT_RING_DISTAL_EXT:
    case XR_HAND_JOINT_LITTLE_DISTAL_EXT:
        return { 0.0025f,0.0025f, 0.0025f };
    case XR_HAND_JOINT_THUMB_DISTAL_EXT:
        return { 0.0035f,0.0035f, 0.0035f };

    case XR_HAND_JOINT_THUMB_TIP_EXT:
    case XR_HAND_JOINT_INDEX_TIP_EXT:
    case XR_HAND_JOINT_MIDDLE_TIP_EXT:
    case XR_HAND_JOINT_RING_TIP_EXT:
    case XR_HAND_JOINT_LITTLE_TIP_EXT:
        return { 0.002f,0.002f, 0.002f };
    default: return { 0.01f, 0.01f,0.01f };
    }
}

constexpr inline XrVector3f GetHandJointNonUniformScale(const XrHandJointEXT jointType) {
    switch (jointType) {
    case XR_HAND_JOINT_WRIST_EXT:
        return { 0.007f,0.007f, 0.009f };

    case XR_HAND_JOINT_PALM_EXT:
        return { 0.0055f,0.0055f, 0.0035f };

    case XR_HAND_JOINT_THUMB_METACARPAL_EXT:
    case XR_HAND_JOINT_INDEX_METACARPAL_EXT:
    case XR_HAND_JOINT_MIDDLE_METACARPAL_EXT:
    case XR_HAND_JOINT_RING_METACARPAL_EXT:
    case XR_HAND_JOINT_LITTLE_METACARPAL_EXT:
        return { 0.0045f,0.0045f, 0.0065f };

    case XR_HAND_JOINT_THUMB_PROXIMAL_EXT:
    case XR_HAND_JOINT_INDEX_PROXIMAL_EXT:
    case XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT:
    case XR_HAND_JOINT_RING_PROXIMAL_EXT:
    case XR_HAND_JOINT_LITTLE_PROXIMAL_EXT:
        return { 0.0035f,0.0035f, 0.0045f };

    case XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT:
    case XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT:
    case XR_HAND_JOINT_RING_INTERMEDIATE_EXT:
    case XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT:
        return { 0.003f,0.003f, 0.004f };

    case XR_HAND_JOINT_INDEX_DISTAL_EXT:
    case XR_HAND_JOINT_MIDDLE_DISTAL_EXT:
    case XR_HAND_JOINT_RING_DISTAL_EXT:
    case XR_HAND_JOINT_LITTLE_DISTAL_EXT:
        return { 0.0025f,0.0025f, 0.0035f };
    case XR_HAND_JOINT_THUMB_DISTAL_EXT:
        return { 0.0035f,0.0035f, 0.004f };

    case XR_HAND_JOINT_THUMB_TIP_EXT:
    case XR_HAND_JOINT_INDEX_TIP_EXT:
    case XR_HAND_JOINT_MIDDLE_TIP_EXT:
    case XR_HAND_JOINT_RING_TIP_EXT:
    case XR_HAND_JOINT_LITTLE_TIP_EXT:
        return { 0.002f,0.002f, 0.0025f };
    default: return { 0.01f, 0.01f,0.01f };
    }
}

}
#endif
