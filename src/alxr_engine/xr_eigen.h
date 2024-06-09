#ifndef AXLR_XR_EIGEN_H
#define AXLR_XR_EIGEN_H
#pragma once

#ifdef Success
#undef Success
#endif  // Success

#include <cmath>
#include <limits>
#include <optional>
#include <Eigen/Core>
#include <Eigen/Dense>
#include "alxr_ctypes.h"

namespace ALXR {

template < typename RealT >
constexpr inline RealT ToDegrees(const RealT radians) {
    return static_cast<RealT>(radians * (180.0 / M_PI));
}

template < typename RealT >
constexpr inline RealT ToRadians(const RealT degrees) {
    return static_cast<RealT>(degrees * (M_PI / 180.0));
}

inline Eigen::Vector2f ToVector2f(const XrVector2f& v) {
    return Eigen::Vector2f{ v.x, v.y };
}

inline XrVector2f ToXrVector2f(const Eigen::Vector2f& v) {
    return XrVector2f{ v.x(), v.y() };
}

inline Eigen::Vector3f ToVector3f(const XrVector3f& v) {
    return Eigen::Vector3f{ v.x, v.y, v.z };
}

inline XrVector3f ToXrVector3f(const Eigen::Vector3f& v) {
    return XrVector3f{ v.x(), v.y(), v.z() };
}

constexpr inline ALXRVector3f ToALXRVector3f(const XrVector3f& v) {
    return { v.x, v.y, v.z };
}

/*constexpr*/ inline ALXRVector3f ToALXRVector3f(const Eigen::Vector3f& v) {
    return { v.x(), v.y(), v.z() };
}

inline Eigen::Quaternionf ToQuaternionf(const XrQuaternionf& q) {
    return Eigen::Quaternionf{ q.w, q.x, q.y, q.z };
}

inline XrQuaternionf ToXrQuaternionf(const Eigen::Quaternionf& q) {
    return XrQuaternionf{ q.x(), q.y(), q.z(), q.w() };
}

constexpr inline ALXRQuaternionf ToALXRQuaternionf(const XrQuaternionf& v) {
    return { v.x, v.y, v.z, v.w };
}

/*constexpr*/ inline ALXRQuaternionf ToALXRQuaternionf(const Eigen::Quaternionf& v) {
    return { v.x(), v.y(), v.z(), v.w() };
}

inline Eigen::Affine3f ToAffine3f(const XrPosef& pose) {
    Eigen::Affine3f pf = Eigen::Affine3f::Identity();
    return pf
        .translate(ToVector3f(pose.position))
        .rotate(ToQuaternionf(pose.orientation));
}

inline Eigen::Matrix4f ToMatrix4f(const XrPosef& pose) {
    return ToAffine3f(pose).matrix();
}

inline XrPosef ToPosef(const Eigen::Affine3f& at) {
    return XrPosef {
        .orientation = ToXrQuaternionf(Eigen::Quaternionf(at.rotation())),
        .position = ToXrVector3f(at.translation()),
    };
}

constexpr inline ALXRPosef ToALXRPosef(const XrPosef& p) {
    return {
        .orientation = ToALXRQuaternionf(p.orientation),
        .position = ToALXRVector3f(p.position),
    };
}

/*constexpr*/ inline ALXRPosef ToALXRPosef(const Eigen::Affine3f& p) {
    return {
        .orientation = ToALXRQuaternionf(Eigen::Quaternionf{p.rotation()}),
        .position = ToALXRVector3f(p.translation()),
    };
}

inline Eigen::Affine3f CreateTRS(const XrPosef& pose, const XrVector3f& scale) {
    Eigen::Affine3f pf = Eigen::Affine3f::Identity();
    return pf.fromPositionOrientationScale(
        ToVector3f(pose.position),
        ToQuaternionf(pose.orientation),
        ToVector3f(scale)
    );
}

enum class GraphicsAPI { Vulkan, OpenGL, OpenGLES, D3D };
// Creates a projection matrix based on the specified dimensions.
// The projection matrix transforms -Z=forward, +Y=up, +X=right to the appropriate clip space for the graphics API.
// The far plane is placed at infinity if farZ <= nearZ.
// An infinite projection matrix is preferred for rasterization because, except for
// things *right* up against the near plane, it always provides better precision:
//              "Tightening the Precision of Perspective Rendering"
//              Paul Upchurch, Mathieu Desbrun
//              Journal of Graphics Tools, Volume 16, Issue 1, 2012
inline Eigen::Matrix4f CreateProjection
(
    const GraphicsAPI graphicsApi,
    const float tanAngleLeft, const float tanAngleRight,
    const float tanAngleUp, const float tanAngleDown,
    const float nearZ, const float farZ
)
{
    using Matrix4 = Eigen::Matrix4f;

    // Computes the tanAngleHeight based on the graphics API. 
    // Set to tanAngleDown - tanAngleUp for a clip space with positive Y down (Vulkan).
    // Set to tanAngleUp - tanAngleDown for a clip space with positive Y up (OpenGL / D3D / Metal).
    constexpr auto computeTanAngleHeight = [](const GraphicsAPI api, const float up, const float down) {
        return api == GraphicsAPI::Vulkan ? (down - up) : (up - down);
    };

    // Computes the offsetZ based on the graphics API. 
    // Set to nearZ for a [-1,1] Z clip space (OpenGL / OpenGL ES).
    // Set to zero for a [0,1] Z clip space (Vulkan / D3D / Metal).
    constexpr auto computeOffsetZ = [](const GraphicsAPI api, const float z) {
        return (api == GraphicsAPI::OpenGL || api == GraphicsAPI::OpenGLES) ? z : 0.0f;
    };

    const float tanAngleWidth = tanAngleRight - tanAngleLeft;
    const float tanAngleHeight = computeTanAngleHeight(graphicsApi, tanAngleUp, tanAngleDown);
    const float offsetZ = computeOffsetZ(graphicsApi, nearZ);

    Matrix4 result = Matrix4::Identity();

    if (farZ <= nearZ) {
        // place the far plane at infinity
        result << 2.0f / tanAngleWidth, 0.0f, (tanAngleRight + tanAngleLeft) / tanAngleWidth, 0.0f,
            0.0f, 2.0f / tanAngleHeight, (tanAngleUp + tanAngleDown) / tanAngleHeight, 0.0f,
            0.0f, 0.0f, -1.0f, -(nearZ + offsetZ),
            0.0f, 0.0f, -1.0f, 0.0f;
    }
    else {
        // normal projection
        result << 2.0f / tanAngleWidth, 0.0f, (tanAngleRight + tanAngleLeft) / tanAngleWidth, 0.0f,
            0.0f, 2.0f / tanAngleHeight, (tanAngleUp + tanAngleDown) / tanAngleHeight, 0.0f,
            0.0f, 0.0f, -(farZ + offsetZ) / (farZ - nearZ), -(farZ * (nearZ + offsetZ)) / (farZ - nearZ),
            0.0f, 0.0f, -1.0f, 0.0f;
    }
    return result;
}

inline Eigen::Matrix4f CreateProjectionFov
(
    const GraphicsAPI graphicsApi,
    const XrFovf& fov,
    const float nearZ, const float farZ
)
{
    const float tanLeft  = std::tan(fov.angleLeft);
    const float tanRight = std::tan(fov.angleRight);
    const float tanDown  = std::tan(fov.angleDown);
    const float tanUp    = std::tan(fov.angleUp);
    return CreateProjection(graphicsApi, tanLeft, tanRight, tanUp, tanDown, nearZ, farZ);
}
}
#endif
