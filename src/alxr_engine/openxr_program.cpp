// Copyright (c) 2017-2021, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#define ENGINE_DLL_EXPORTS

#include "pch.h"
#include "common.h"
#include "options.h"
#include "platformdata.h"
#include "platformplugin.h"
#include "graphicsplugin.h"
#include "openxr_program.h"
#include "xr_eigen.h"
#include <type_traits>
#include <array>
#include <cmath>
#include <cstddef>
#include <cinttypes>
#include <cassert>
#include <cstring>
#include <ctime>
#include <tuple>
#include <numeric>
#include <span>
#include <unordered_map>
#include <map>
#include <string_view>
#include <string>
#include <ratio>
#include <chrono>
#include <algorithm>
#include <mutex>
#include <shared_mutex>
#ifdef XR_USE_PLATFORM_ANDROID
    #include <unistd.h>
#endif

#include "vrcft_proxy_server.h"

#include "xr_utils.h"
#include "concurrent_queue.h"
//#include "alxr_engine.h"
#include "alxr_ctypes.h"
#include "alxr_facial_eye_tracking_packet.h"
#include "ALVR-common/packet_types.h"
#include "timing.h"
#include "latency_manager.h"
#include "interaction_profiles.h"
#include "interaction_manager.h"
#include "eye_gaze_interaction.h"

#ifdef XR_USE_PLATFORM_ANDROID
#ifndef ALXR_ENGINE_DISABLE_QUIT_ACTION
#define ALXR_ENGINE_DISABLE_QUIT_ACTION
#endif
#endif

//#define ALXR_ENGINE_ENABLE_VIZ_SPACES

namespace {
#if !defined(XR_USE_PLATFORM_WIN32)
#define strcpy_s(dest, source) strncpy((dest), (source), sizeof(dest))
#endif

inline std::string GetXrVersionString(XrVersion ver) {
    return Fmt("%d.%d.%d", XR_VERSION_MAJOR(ver), XR_VERSION_MINOR(ver), XR_VERSION_PATCH(ver));
}

inline XrFormFactor GetXrFormFactor(const std::string& formFactorStr) {
    if (EqualsIgnoreCase(formFactorStr, "Hmd")) {
        return XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    }
    if (EqualsIgnoreCase(formFactorStr, "Handheld")) {
        return XR_FORM_FACTOR_HANDHELD_DISPLAY;
    }
    throw std::invalid_argument(Fmt("Unknown form factor '%s'", formFactorStr.c_str()));
}

inline XrViewConfigurationType GetXrViewConfigurationType(const std::string& viewConfigurationStr) {
    if (EqualsIgnoreCase(viewConfigurationStr, "Mono")) {
        return XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO;
    }
    if (EqualsIgnoreCase(viewConfigurationStr, "Stereo")) {
        return XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    }
    throw std::invalid_argument(Fmt("Unknown view configuration '%s'", viewConfigurationStr.c_str()));
}

inline XrEnvironmentBlendMode GetXrEnvironmentBlendMode(const std::string& environmentBlendModeStr) {
    if (EqualsIgnoreCase(environmentBlendModeStr, "Opaque")) {
        return XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    }
    if (EqualsIgnoreCase(environmentBlendModeStr, "Additive")) {
        return XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
    }
    if (EqualsIgnoreCase(environmentBlendModeStr, "AlphaBlend")) {
        return XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
    }
    throw std::invalid_argument(Fmt("Unknown environment blend mode '%s'", environmentBlendModeStr.c_str()));
}

namespace Math {
namespace Pose {
constexpr bool IsPoseValid(XrSpaceLocationFlags locationFlags) {
    constexpr const XrSpaceLocationFlags PoseValidFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (locationFlags & PoseValidFlags) == PoseValidFlags;
}

constexpr bool IsPoseTracked(XrSpaceLocationFlags locationFlags) {
    constexpr const XrSpaceLocationFlags PoseTrackedFlags =
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    return (locationFlags & PoseTrackedFlags) == PoseTrackedFlags;
}

constexpr bool IsPoseValid(const XrSpaceLocation& spaceLocation) {
    return IsPoseValid(spaceLocation.locationFlags);
}

constexpr bool IsPoseTracked(const XrSpaceLocation& spaceLocation) {
    return IsPoseTracked(spaceLocation.locationFlags);
}

constexpr bool IsPoseValid(const XrHandJointLocationEXT& jointLocation) {
    return IsPoseValid(jointLocation.locationFlags);
}

constexpr bool IsPoseTracked(const XrHandJointLocationEXT& jointLocation) {
    return IsPoseTracked(jointLocation.locationFlags);
}
}  // namespace Pose
}  // namespace Math

constexpr inline auto ToTrackingSpaceName(const ALXRTrackingSpace ts)
{
    switch (ts)
    {
    case ALXRTrackingSpace::LocalRefSpace: return "ALXRLocal";
    case ALXRTrackingSpace::ViewRefSpace: return "View";
    }
    return "Stage";
}

/*constexpr*/ inline ALXRTrackingSpace ToTrackingSpace(const std::string_view& tsname)
{
    if (EqualsIgnoreCase(tsname, "Local") || 
        EqualsIgnoreCase(tsname, "LocalFloor") ||
        EqualsIgnoreCase(tsname, "ALXRLocal"))
        return ALXRTrackingSpace::LocalRefSpace;
    if (EqualsIgnoreCase(tsname, "View"))
        return ALXRTrackingSpace::ViewRefSpace;
    return ALXRTrackingSpace::StageRefSpace;
}

constexpr inline ALXRTrackingSpace ToTrackingSpace(const XrReferenceSpaceType xrreftype)
{
    switch (xrreftype) {
    case XR_REFERENCE_SPACE_TYPE_VIEW: return ALXRTrackingSpace::ViewRefSpace;
    case XR_REFERENCE_SPACE_TYPE_LOCAL: return ALXRTrackingSpace::LocalRefSpace;
    }
    return ALXRTrackingSpace::StageRefSpace;
}

constexpr inline XrReferenceSpaceType ToXrReferenceSpaceType(const ALXRTrackingSpace xrreftype)
{
    switch (xrreftype) {
    case ALXRTrackingSpace::ViewRefSpace: return XR_REFERENCE_SPACE_TYPE_VIEW;
    case ALXRTrackingSpace::LocalRefSpace: return XR_REFERENCE_SPACE_TYPE_LOCAL;
    }
    return XR_REFERENCE_SPACE_TYPE_STAGE;
}

inline XrReferenceSpaceCreateInfo GetXrReferenceSpaceCreateInfo(const std::string_view& referenceSpaceTypeStr) {
    constexpr auto Translation = [](const XrVector3f& translation) -> XrPosef {
        XrPosef t = ALXR::IdentityPose;
        t.position = translation;
        return t;
    };
    constexpr auto RotateCCWAboutYAxis = [](float radians, XrVector3f translation) -> XrPosef {
        return XrPosef {
            .orientation {
                .x = 0.f,
                .y = std::sin(radians * 0.5f),
                .z = 0.f,
                .w = std::cos(radians * 0.5f),
            },
            .position = translation,
        };
    };
    XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{
        .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .next = nullptr,
        .poseInReferenceSpace = ALXR::IdentityPose
    };
    if (EqualsIgnoreCase(referenceSpaceTypeStr, "View")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "ViewFront")) {
        // Render head-locked 2m in front of device.
        referenceSpaceCreateInfo.poseInReferenceSpace = Translation({ 0.f, 0.f, -2.f });
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "LocalFloor")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Local")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "ALXRLocal")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = Translation({ 0.f, -1.4f,0.f });
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Stage")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeft")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = RotateCCWAboutYAxis(0.f, {-2.f, 0.f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRight")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = RotateCCWAboutYAxis(0.f, {2.f, 0.f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeftRotated")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = RotateCCWAboutYAxis(3.14f / 3.f, {-2.f, 0.5f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRightRotated")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = RotateCCWAboutYAxis(-3.14f / 3.f, {2.f, 0.5f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "UboundedMSFT")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT;
    } else {
        throw std::invalid_argument(Fmt("Unknown reference space type '%s'", referenceSpaceTypeStr.data()));
    }
    return referenceSpaceCreateInfo;
}

inline XrReferenceSpaceCreateInfo GetXrReferenceSpaceCreateInfo(const ALXRTrackingSpace ts) {
    return GetXrReferenceSpaceCreateInfo(ToTrackingSpaceName(ts));
}

constexpr inline ALXRVector3f ToALXRVector3f(const XrVector3f& v) {
    return { v.x, v.y, v.z };
}

/*constexpr*/ inline ALXRVector3f ToALXRVector3f(const Eigen::Vector3f& v) {
    return { v.x(), v.y(), v.z() };
}

constexpr inline ALXRQuaternionf ToALXRQuaternionf(const XrQuaternionf& v) {
    return { v.x, v.y, v.z, v.w };
}

/*constexpr*/ inline ALXRQuaternionf ToALXRQuaternionf(const Eigen::Quaternionf& v) {
    return { v.x(), v.y(), v.z(), v.w() };
}

constexpr inline ALXRPosef ToALXRPosef(const XrPosef& p) {
    return { 
        .orientation = ToALXRQuaternionf(p.orientation),
        .position    = ToALXRVector3f(p.position),
    };
}

/*constexpr*/ inline ALXRPosef ToALXRPosef(const Eigen::Affine3f& p) {
    return {
        .orientation = ToALXRQuaternionf(Eigen::Quaternionf{p.rotation()}),
        .position    = ToALXRVector3f(p.translation()),
    };
}

constexpr inline XrHandJointEXT GetJointParent(const XrHandJointEXT h)
{
    switch (h)
    {
    case XR_HAND_JOINT_PALM_EXT: return XR_HAND_JOINT_PALM_EXT;
    case XR_HAND_JOINT_WRIST_EXT: return XR_HAND_JOINT_PALM_EXT;
    case XR_HAND_JOINT_THUMB_METACARPAL_EXT: return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_THUMB_PROXIMAL_EXT: return XR_HAND_JOINT_THUMB_METACARPAL_EXT;
    case XR_HAND_JOINT_THUMB_DISTAL_EXT: return XR_HAND_JOINT_THUMB_PROXIMAL_EXT;
    case XR_HAND_JOINT_THUMB_TIP_EXT: return XR_HAND_JOINT_THUMB_DISTAL_EXT;
    case XR_HAND_JOINT_INDEX_METACARPAL_EXT: return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_INDEX_PROXIMAL_EXT: return XR_HAND_JOINT_INDEX_METACARPAL_EXT;
    case XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT: return XR_HAND_JOINT_INDEX_PROXIMAL_EXT;
    case XR_HAND_JOINT_INDEX_DISTAL_EXT: return XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT;
    case XR_HAND_JOINT_INDEX_TIP_EXT: return XR_HAND_JOINT_INDEX_DISTAL_EXT;
    case XR_HAND_JOINT_MIDDLE_METACARPAL_EXT: return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT: return XR_HAND_JOINT_MIDDLE_METACARPAL_EXT;
    case XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT: return XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT;
    case XR_HAND_JOINT_MIDDLE_DISTAL_EXT: return XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT;
    case XR_HAND_JOINT_MIDDLE_TIP_EXT: return XR_HAND_JOINT_MIDDLE_DISTAL_EXT;
    case XR_HAND_JOINT_RING_METACARPAL_EXT: return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_RING_PROXIMAL_EXT: return XR_HAND_JOINT_RING_METACARPAL_EXT;
    case XR_HAND_JOINT_RING_INTERMEDIATE_EXT: return XR_HAND_JOINT_RING_PROXIMAL_EXT;
    case XR_HAND_JOINT_RING_DISTAL_EXT: return XR_HAND_JOINT_RING_INTERMEDIATE_EXT;
    case XR_HAND_JOINT_RING_TIP_EXT: return XR_HAND_JOINT_RING_DISTAL_EXT;
    case XR_HAND_JOINT_LITTLE_METACARPAL_EXT: return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_LITTLE_PROXIMAL_EXT: return XR_HAND_JOINT_LITTLE_METACARPAL_EXT;
    case XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT: return XR_HAND_JOINT_LITTLE_PROXIMAL_EXT;
    case XR_HAND_JOINT_LITTLE_DISTAL_EXT: return XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT;
    case XR_HAND_JOINT_LITTLE_TIP_EXT: return XR_HAND_JOINT_LITTLE_DISTAL_EXT;
    }
    return h;
}

constexpr inline XrHandJointEXT ToXRHandJointType(const ALVR_HAND h)
{
    switch (h)
    {
    case ALVR_HAND::alvrHandBone_WristRoot: return XR_HAND_JOINT_WRIST_EXT;
    case ALVR_HAND::alvrHandBone_Thumb0: return XR_HAND_JOINT_THUMB_METACARPAL_EXT;
    case ALVR_HAND::alvrHandBone_Thumb1: return XR_HAND_JOINT_THUMB_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Thumb2: return XR_HAND_JOINT_THUMB_DISTAL_EXT;
    case ALVR_HAND::alvrHandBone_Thumb3: return XR_HAND_JOINT_THUMB_TIP_EXT;
    case ALVR_HAND::alvrHandBone_Index1: return XR_HAND_JOINT_INDEX_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Index2: return XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT;
    case ALVR_HAND::alvrHandBone_Index3: return XR_HAND_JOINT_INDEX_DISTAL_EXT;
    case ALVR_HAND::alvrHandBone_Middle1: return XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Middle2: return XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT;
    case ALVR_HAND::alvrHandBone_Middle3: return XR_HAND_JOINT_MIDDLE_DISTAL_EXT;
    case ALVR_HAND::alvrHandBone_Ring1: return XR_HAND_JOINT_RING_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Ring2: return XR_HAND_JOINT_RING_INTERMEDIATE_EXT;
    case ALVR_HAND::alvrHandBone_Ring3: return XR_HAND_JOINT_RING_DISTAL_EXT;
    case ALVR_HAND::alvrHandBone_Pinky0: return XR_HAND_JOINT_LITTLE_METACARPAL_EXT;
    case ALVR_HAND::alvrHandBone_Pinky1: return XR_HAND_JOINT_LITTLE_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Pinky2: return XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT;
    case ALVR_HAND::alvrHandBone_Pinky3: return XR_HAND_JOINT_LITTLE_DISTAL_EXT;
    default: return XR_HAND_JOINT_MAX_ENUM_EXT;
    }
}

constexpr inline auto make_local_dimming_info(const bool enabled) {
    return XrLocalDimmingFrameEndInfoMETA{
        .type = XR_TYPE_LOCAL_DIMMING_FRAME_END_INFO_META,
        .next = nullptr,
        .localDimmingMode = enabled ? XR_LOCAL_DIMMING_MODE_ON_META : XR_LOCAL_DIMMING_MODE_OFF_META
    };
}

struct OpenXrProgram final : IOpenXrProgram {
    OpenXrProgram(const std::shared_ptr<Options>& options, const std::shared_ptr<IPlatformPlugin>& platformPlugin,
        const std::shared_ptr<IGraphicsPlugin>& graphicsPlugin)
        : m_options(options), m_platformPlugin(platformPlugin), m_graphicsPlugin(graphicsPlugin)
        , xrLocalDimmingFrameEndInfoMETA(make_local_dimming_info(!options->DisableLocalDimming))
    {
        LogLayersAndExtensions();
    }

    OpenXrProgram(const std::shared_ptr<Options>& options, const std::shared_ptr<IPlatformPlugin>& platformPlugin)
        : m_options(options), m_platformPlugin(platformPlugin), m_graphicsPlugin{ nullptr }
        , xrLocalDimmingFrameEndInfoMETA(make_local_dimming_info(!options->DisableLocalDimming))
    {
        assert(m_options);
        LogLayersAndExtensions();
        
        const bool headlessRequested = m_options && m_options->EnableHeadless();
        const bool enableTrueHeadless = headlessRequested && IsExtEnabled(XR_MND_HEADLESS_EXTENSION_NAME);

        auto& graphicsApi = options->GraphicsPlugin;
        if (!enableTrueHeadless && (graphicsApi.empty() || graphicsApi == "auto"))
        {
            Log::Write(Log::Level::Info, "Running auto graphics api selection.");
            constexpr const auto to_graphics_api_str = [](const ALXRGraphicsApi gapi) -> std::tuple<std::string_view, std::string_view>
            {
                using namespace std::string_view_literals;
                switch (gapi)
                {
                case ALXRGraphicsApi::Vulkan:   return std::make_tuple("XR_KHR_vulkan_enable"sv, "Vulkan"sv);
                case ALXRGraphicsApi::D3D12:    return std::make_tuple("XR_KHR_D3D12_enable"sv, "D3D12"sv);
                case ALXRGraphicsApi::D3D11:    return std::make_tuple("XR_KHR_D3D11_enable"sv, "D3D11"sv);
                case ALXRGraphicsApi::Vulkan2:
                    [[fallthrough]];
                default: return std::make_tuple("XR_KHR_vulkan_enable2"sv, "Vulkan2"sv);
                }
            };
            using ALXRGraphicsApiList = std::array<ALXRGraphicsApi, size_t(ALXRGraphicsApi::ApiCount)>;
            static constexpr const ALXRGraphicsApiList GApiListSelectionOrder =
                { ALXRGraphicsApi::Vulkan2, ALXRGraphicsApi::Vulkan, ALXRGraphicsApi::D3D12, ALXRGraphicsApi::D3D11, }; 
            static constexpr const ALXRGraphicsApiList WinGApiListSelectionOrder =
                { ALXRGraphicsApi::D3D12, ALXRGraphicsApi::D3D11, ALXRGraphicsApi::Vulkan2, ALXRGraphicsApi::Vulkan, };
#ifdef XR_USE_PLATFORM_WIN32
            // Until Vulkan-D3D11VA interop is improved and/or Vulkan-video exts (via ffmpeg) are supported, use different selection order prioritizing D3D apis.
            const auto& gapiListSelectionOrder = !options->SimulateHeadless ? WinGApiListSelectionOrder : GApiListSelectionOrder;
#else
            const auto& gapiListSelectionOrder = GApiListSelectionOrder;
#endif
            for (const auto apiIndex : gapiListSelectionOrder) {
                const auto& [ext_name, gapi] = to_graphics_api_str(static_cast<ALXRGraphicsApi>(apiIndex));
                auto itr = m_supportedGraphicsContexts.find(ext_name);
                if (itr != m_supportedGraphicsContexts.end() && itr->second) {
                    graphicsApi = gapi;
                    break;
                }
            }
        }

        if (enableTrueHeadless) {
            graphicsApi = "Headless";
        }
        m_graphicsPlugin = CreateGraphicsPlugin(options, platformPlugin);
        
        if (enableTrueHeadless) {
            assert(graphicsApi == "Headless");
            Log::Write(Log::Level::Info, "Headless session selected, no graphics API has been setup.");
        } else {
            Log::Write(Log::Level::Info, Fmt("Selected Graphics API: %s", graphicsApi.c_str()));
        }
    }

    virtual ~OpenXrProgram() override {
        Log::Write(Log::Level::Verbose, "Destroying OpenXrProgram");
        
        if (IsSessionRunning()) {
            xrEndSession(m_session);
            m_sessionRunning.store(false);
        }

        if (m_ptLayerData.reconPassthroughLayer != XR_NULL_HANDLE) {
            Log::Write(Log::Level::Verbose, "Destroying PassthroughLayer");
            assert(m_pfnDestroyPassthroughLayerFB);
            m_pfnDestroyPassthroughLayerFB(m_ptLayerData.reconPassthroughLayer);
            m_ptLayerData.reconPassthroughLayer = XR_NULL_HANDLE;
        }

        if (m_ptLayerData.passthrough != XR_NULL_HANDLE) {
            Log::Write(Log::Level::Verbose, "Destroying Passthrough");
            assert(m_pfnDestroyPassthroughFB);
            m_pfnDestroyPassthroughFB(m_ptLayerData.passthrough);
            m_ptLayerData.passthrough = XR_NULL_HANDLE;
        }

        if (m_ptLayerData.passthroughHTC != XR_NULL_HANDLE) {
            Log::Write(Log::Level::Verbose, "Destroying HTC Passthrough");
            assert(m_pfnDestroyPassthroughHTC);
            m_pfnDestroyPassthroughHTC(m_ptLayerData.passthroughHTC);
            m_ptLayerData.passthroughHTC = XR_NULL_HANDLE;
        }

        if (m_pfnDestroyHandTrackerEXT != nullptr)
        {
            Log::Write(Log::Level::Verbose, "Destroying HandTrackers");
            assert(m_pfnCreateHandTrackerEXT != nullptr);
            for (auto& handTracker : m_input.handTrackers) {
                if (handTracker.tracker != XR_NULL_HANDLE) {
                    m_pfnDestroyHandTrackerEXT(handTracker.tracker);
                    handTracker.tracker = XR_NULL_HANDLE;
                }
            }
        }

        if (m_eyeTrackerANDROID != XR_NULL_HANDLE)
        {
            Log::Write(Log::Level::Verbose, "Destroying EyeTrackerANDROID");
            assert(xrDestroyEyeTrackerANDROID != nullptr);
            xrDestroyEyeTrackerANDROID(m_eyeTrackerANDROID);
            m_eyeTrackerANDROID = XR_NULL_HANDLE;
        }

        if (eyeTrackerFB_ != XR_NULL_HANDLE)
        {
            Log::Write(Log::Level::Verbose, "Destroying EyeTracker");
            assert(m_xrDestroyEyeTrackerFB_ != nullptr);
            m_xrDestroyEyeTrackerFB_(eyeTrackerFB_);
            eyeTrackerFB_ = XR_NULL_HANDLE;
        }

        if (faceTrackerFBV2_ != XR_NULL_HANDLE) {
            Log::Write(Log::Level::Verbose, "Destroying FaceTrackerFB_V2");
            assert(m_xrDestroyFaceTracker2FB_ != nullptr);
            m_xrDestroyFaceTracker2FB_(faceTrackerFBV2_);
        }

        if (faceTrackerFB_ != XR_NULL_HANDLE)
        {
            Log::Write(Log::Level::Verbose, "Destroying FaceTrackerFB");
            assert(m_xrDestroyFaceTrackerFB_ != nullptr);
            m_xrDestroyFaceTrackerFB_(faceTrackerFB_);
            faceTrackerFB_ = XR_NULL_HANDLE;
        }

        if (m_faceTrackerANDROID != XR_NULL_HANDLE)
        {
            Log::Write(Log::Level::Verbose, "Destroying FaceTrackerANDROID");
            assert(xrDestroyFaceTrackerANDROID != nullptr);
            xrDestroyFaceTrackerANDROID(m_faceTrackerANDROID);
            m_faceTrackerANDROID = XR_NULL_HANDLE;
        }

        for (auto& facialTracker : m_facialTrackersHTC) {
            if (facialTracker != XR_NULL_HANDLE) {
                Log::Write(Log::Level::Verbose, "Destroying FacialTrackerHTC");
                assert(m_xrDestroyFacialTrackerHTC != nullptr);
                m_xrDestroyFacialTrackerHTC(facialTracker);
                facialTracker = XR_NULL_HANDLE;
            }
        }

        m_vrcftProxyServer.reset();
        
        m_interactionManager.reset();

        if (m_visualizedSpaces.size() > 0) {
            Log::Write(Log::Level::Verbose, "Destroying Visualized XrSpaces");
        }
        for (XrSpace visualizedSpace : m_visualizedSpaces) {
            xrDestroySpace(visualizedSpace);
        }
        m_visualizedSpaces.clear();

        if (m_viewSpace != XR_NULL_HANDLE) {
            Log::Write(Log::Level::Verbose, "Destroying View XrSpaces");
            xrDestroySpace(m_viewSpace);
            m_viewSpace = XR_NULL_HANDLE;
        }

        if (m_boundingStageSpace != XR_NULL_HANDLE) {
            Log::Write(Log::Level::Verbose, "Destroying BoundingStage XrSpaces");
            xrDestroySpace(m_boundingStageSpace);
            m_boundingStageSpace = XR_NULL_HANDLE;
        }

        if (m_appSpace != XR_NULL_HANDLE) {
            Log::Write(Log::Level::Verbose, "Destroying App XrSpaces");
            xrDestroySpace(m_appSpace);
            m_appSpace = XR_NULL_HANDLE;
        }

        Log::Write(Log::Level::Verbose, "Destroying XrSwapChains");
        ClearSwapchains();

        if (m_session != XR_NULL_HANDLE) {
            Log::Write(Log::Level::Verbose, "Destroying XrSession");
            xrDestroySession(m_session);
            m_session = XR_NULL_HANDLE;
        }

        if (m_instance != XR_NULL_HANDLE) {
            Log::Write(Log::Level::Verbose, "Destroying XrInstance");
            xrDestroyInstance(m_instance);
            m_instance = XR_NULL_HANDLE;
        }

        Log::Write(Log::Level::Verbose, "Destroying GraphicsPlugin");
        m_graphicsPlugin.reset();
        Log::Write(Log::Level::Verbose, "Destroying PlatformPlugin");
        m_platformPlugin.reset();

        m_systemId = XR_NULL_SYSTEM_ID;

        Log::Write(Log::Level::Verbose, "OpenXrProgram Destroyed.");
    }

    static constexpr const std::array<const XrEnvironmentBlendMode, 2> NonOpaqueBlendModes = {
        XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,
        XR_ENVIRONMENT_BLEND_MODE_ADDITIVE
    };

    using ExtensionMap = std::unordered_map<std::string_view, bool>;
    ExtensionMap m_availableSupportedExtMap = {
        // KHR extensions
        { XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME, false },
#ifdef XR_USE_PLATFORM_WIN32
        { XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME, false },
#endif
#ifdef XR_USE_PLATFORM_ANDROID
        { XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME, false },
#endif
        { XR_KHR_VISIBILITY_MASK_EXTENSION_NAME, false },
        // EXT extensions
#ifdef XR_USE_PLATFORM_UWP
#pragma message ("UWP Extensions Enabled.")
        // Require XR_EXT_win32_appcontainer_compatible extension when building in UWP context.
        { XR_EXT_WIN32_APPCONTAINER_COMPATIBLE_EXTENSION_NAME, false },
#endif
        { XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME, false },
        { XR_EXT_LOCAL_FLOOR_EXTENSION_NAME, false },
        { XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME, false },
        { XR_EXT_HAND_TRACKING_EXTENSION_NAME, false },

        { XR_ANDROID_AVATAR_EYES_EXTENSION_NAME, false },
        { XR_ANDROID_FACE_TRACKING_EXTENSION_NAME, false },

        { XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, false },
        { XR_FB_COLOR_SPACE_EXTENSION_NAME, false },
        { XR_FB_PASSTHROUGH_EXTENSION_NAME, false },
        { XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME, false },
        { XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME, false },
        { XR_FB_FACE_TRACKING2_EXTENSION_NAME, false },
        { XR_FB_FACE_TRACKING_EXTENSION_NAME, false },
        { XR_META_LOCAL_DIMMING_EXTENSION_NAME, false },

        { XR_HTC_VIVE_COSMOS_CONTROLLER_INTERACTION_EXTENSION_NAME, false },
        { XR_HTC_VIVE_FOCUS3_CONTROLLER_INTERACTION_EXTENSION_NAME, false },
        { XR_HTC_PASSTHROUGH_EXTENSION_NAME, false },
        { XR_HTC_FACIAL_TRACKING_EXTENSION_NAME, false },

        { XR_ML_ML2_CONTROLLER_INTERACTION_EXTENSION_NAME, false },
        { XR_MND_HEADLESS_EXTENSION_NAME, false },
        { XR_MSFT_UNBOUNDED_REFERENCE_SPACE_EXTENSION_NAME, false },

        { XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME, false },
#ifdef XR_USE_OXR_PICO
        { XR_PICO_BOUNDARY_EXT_EXTENSION_NAME, false },
        { "XR_PICO_boundary", false },
#endif
    };
    ExtensionMap m_supportedGraphicsContexts = {
#ifdef XR_USE_GRAPHICS_API_VULKAN
        { XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,   false },
        { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,    false },
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
        { XR_KHR_D3D12_ENABLE_EXTENSION_NAME,     false },
#endif
#ifdef XR_USE_GRAPHICS_API_D3D11
        { XR_KHR_D3D11_ENABLE_EXTENSION_NAME,     false },
#endif
    };

    void LogLayersAndExtensions() {
        // Write out extension properties for a given layer.
        const auto logExtensions = [this](const char* layerName, int indent = 0) {
            uint32_t instanceExtensionCount = 0;
            CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(layerName, 0, &instanceExtensionCount, nullptr));

            std::vector<XrExtensionProperties> extensions(instanceExtensionCount, {
                .type = XR_TYPE_EXTENSION_PROPERTIES,
                .next = nullptr
            });
            CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(layerName, (uint32_t)extensions.size(), &instanceExtensionCount,
                extensions.data()));

            constexpr const auto SetExtensionMap = [](auto& extMap, const std::string_view extName)
            {
                const auto itr = extMap.find(extName);
                if (itr == extMap.end())
                    return;
                itr->second = true;
            };
            const std::string indentStr(indent, ' ');
            Log::Write(Log::Level::Verbose, Fmt("%sAvailable Extensions: (%d)", indentStr.c_str(), instanceExtensionCount));
            for (const XrExtensionProperties& extension : extensions) {

                SetExtensionMap(m_availableSupportedExtMap,  extension.extensionName);
                SetExtensionMap(m_supportedGraphicsContexts, extension.extensionName);
                Log::Write(Log::Level::Verbose, Fmt("%s  Name=%s SpecVersion=%d", indentStr.c_str(), extension.extensionName,
                    extension.extensionVersion));
            }
        };

        // Log non-layer extensions (layerName==nullptr).
        logExtensions(nullptr);

        // Log layers and any of their extensions.
        {
            uint32_t layerCount = 0;
            CHECK_XRCMD(xrEnumerateApiLayerProperties(0, &layerCount, nullptr));
            std::vector<XrApiLayerProperties> layers(layerCount, {
                .type = XR_TYPE_API_LAYER_PROPERTIES,
                .next = nullptr
            });
            CHECK_XRCMD(xrEnumerateApiLayerProperties((uint32_t)layers.size(), &layerCount, layers.data()));

            Log::Write(Log::Level::Info, Fmt("Available Layers: (%d)", layerCount));
            for (const XrApiLayerProperties& layer : layers) {
                Log::Write(Log::Level::Verbose,
                    Fmt("  Name=%s SpecVersion=%s LayerVersion=%d Description=%s", layer.layerName,
                        GetXrVersionString(layer.specVersion).c_str(), layer.layerVersion, layer.description));
                logExtensions(layer.layerName, 4);
            }
        }
    }

    inline bool IsRuntime(const OxrRuntimeType rtType) const {
        return rtType == m_runtimeType;
    }

    template < const std::size_t major, const std::size_t minor >
    inline bool IsPrePicoPUI() const {
#ifdef XR_USE_OXR_PICO
        assert(IsRuntime(OxrRuntimeType::Pico));
        static constexpr const FirmwareVersion PuiVersion{ major,minor,0 };
        return m_options->firmwareVersion < PuiVersion;
#else
        return false;
#endif
    }

    void LogInstanceInfo() {
        CHECK(m_instance != XR_NULL_HANDLE && m_graphicsPlugin != nullptr);

        XrInstanceProperties instanceProperties{
            .type = XR_TYPE_INSTANCE_PROPERTIES,
            .next = nullptr
        };
        CHECK_XRCMD(xrGetInstanceProperties(m_instance, &instanceProperties));

        Log::Write(Log::Level::Info, Fmt("Instance RuntimeName=%s RuntimeVersion=%s", instanceProperties.runtimeName,
            GetXrVersionString(instanceProperties.runtimeVersion).c_str()));

        m_runtimeType = FromString(instanceProperties.runtimeName);

        Log::Write(Log::Level::Verbose, Fmt("Detected runtime type: %s", ToString(m_runtimeType)));

        const bool enableSRGBLinearization = [this]() {
            if (IsPrePicoPUI<5,4>())
                return false;
            return !m_options->DisableLinearizeSrgb;
        }();
        m_graphicsPlugin->SetEnableLinearizeRGB(enableSRGBLinearization);
#ifdef XR_USE_OXR_PICO
        if (IsPrePicoPUI<5, 8>()) {
            m_graphicsPlugin->SetMaskModeParams({ 0.11f, 0.11f, 0.11f });
            m_graphicsPlugin->SetBlendModeParams(0.62f);
        }
#endif
        m_graphicsPlugin->SetCmdBufferWaitNextFrame(!IsRuntime(OxrRuntimeType::MagicLeap));
    }

    void CreateInstanceInternal() {
        CHECK(m_instance == XR_NULL_HANDLE);

        // Create union of extensions required by platform and graphics plugins.
        std::vector<const char*> extensions;

        // Transform platform and graphics extension std::strings to C strings.
        const std::vector<std::string> platformExtensions = m_platformPlugin->GetInstanceExtensions();
        std::transform(platformExtensions.begin(), platformExtensions.end(), std::back_inserter(extensions),
            [](const std::string& ext) { return ext.c_str(); });
        const std::vector<std::string> graphicsExtensions = m_graphicsPlugin->GetInstanceExtensions();
        std::transform(graphicsExtensions.begin(), graphicsExtensions.end(), std::back_inserter(extensions),
            [](const std::string& ext) { return ext.c_str(); });

        using ExcludeMap = std::unordered_map<std::string_view, bool>;
        const auto exclusionMap = [this]() -> ExcludeMap {
            if (m_options == nullptr)
                return {};
            return {
                { XR_KHR_VISIBILITY_MASK_EXTENSION_NAME,      !m_options->EnableVisibilityMasks() },
                { XR_EXT_HAND_TRACKING_EXTENSION_NAME,        m_options->NoHandTracking || IsPrePicoPUI<5,7>() },
                { XR_MND_HEADLESS_EXTENSION_NAME,             !m_options->EnableHeadless()},
                { XR_FB_PASSTHROUGH_EXTENSION_NAME,           m_options->NoPassthrough },
                { XR_HTC_PASSTHROUGH_EXTENSION_NAME,          m_options->NoPassthrough },
                { XR_HTC_FACIAL_TRACKING_EXTENSION_NAME,      !m_options->IsSelected(ALXRFacialExpressionType::HTC) },
                { XR_FB_FACE_TRACKING2_EXTENSION_NAME,        !m_options->IsSelected(ALXRFacialExpressionType::FB_V2) },
                { XR_FB_FACE_TRACKING_EXTENSION_NAME,         !m_options->IsSelected(ALXRFacialExpressionType::FB) },
                { XR_ANDROID_FACE_TRACKING_EXTENSION_NAME,    !m_options->IsSelected(ALXRFacialExpressionType::Android) },
                { XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME,   !m_options->IsSelected(ALXREyeTrackingType::FBEyeTrackingSocial) },
                { XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME, !m_options->IsSelected(ALXREyeTrackingType::ExtEyeGazeInteraction) },
                { XR_ANDROID_AVATAR_EYES_EXTENSION_NAME,      !m_options->IsSelected(ALXREyeTrackingType::AndroidAvatarEyes) },
            };
        }();
        const auto IsExcluded = [&exclusionMap](const std::string_view& extName) {
            const auto excludeItr = exclusionMap.find(extName);
            return excludeItr != exclusionMap.end() && excludeItr->second;
        };
        Log::Write(Log::Level::Warning, "Excluded Extensions (user specified):");
        for (const auto& [extName, extAvaileble] : m_availableSupportedExtMap) {
            if (IsExcluded(extName)) {
                const std::string name{ extName };
                Log::Write(Log::Level::Warning, Fmt("\t%s", name.c_str()));
                continue;
            }
            if (extAvaileble) {
                extensions.push_back(extName.data());
            }
        }

        Log::Write(Log::Level::Info, "Selected extensions to enable:");
        for (const auto& extName : extensions) {
            const std::string name{ extName };
            Log::Write(Log::Level::Info, Fmt("\t%s", name.c_str()));
        }

        const XrVersion apiVersion = (m_options && m_options->XrApiVersion) ?
            m_options->XrApiVersion : XR_API_VERSION_1_0;

        XrInstanceCreateInfo createInfo {
            .type = XR_TYPE_INSTANCE_CREATE_INFO,
            .next = m_platformPlugin->GetInstanceCreateExtension(),
            .applicationInfo {
                .applicationVersion = 1,
                .engineVersion = 1,
                .apiVersion = apiVersion,
            },
            .enabledApiLayerCount = 0,
            .enabledApiLayerNames = nullptr,
            .enabledExtensionCount = (uint32_t)extensions.size(),
            .enabledExtensionNames = extensions.data()
        };
        auto& appInfo = createInfo.applicationInfo;
        std::strcpy(appInfo.applicationName, "alxr-client");
        std::strcpy(appInfo.engineName, "alxr-engine");
        CHECK_XRCMD(xrCreateInstance(&createInfo, &m_instance));
    }

    void CreateInstance() override {
        //LogLayersAndExtensions();

        CreateInstanceInternal();

        LogInstanceInfo();
    }

    void LogViewConfigurations() {
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId != XR_NULL_SYSTEM_ID);

        uint32_t viewConfigTypeCount = 0;
        CHECK_XRCMD(xrEnumerateViewConfigurations(m_instance, m_systemId, 0, &viewConfigTypeCount, nullptr));
        std::vector<XrViewConfigurationType> viewConfigTypes(viewConfigTypeCount, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO);
        CHECK_XRCMD(xrEnumerateViewConfigurations(m_instance, m_systemId, viewConfigTypeCount, &viewConfigTypeCount,
            viewConfigTypes.data()));
        CHECK((uint32_t)viewConfigTypes.size() == viewConfigTypeCount);

        Log::Write(Log::Level::Info, Fmt("Available View Configuration Types: (%d)", viewConfigTypeCount));
        for (XrViewConfigurationType viewConfigType : viewConfigTypes) {
            Log::Write(Log::Level::Verbose, Fmt("  View Configuration Type: %s %s", to_string(viewConfigType),
                viewConfigType == m_viewConfigType ? "(Selected)" : ""));

            XrViewConfigurationProperties viewConfigProperties{ .type=XR_TYPE_VIEW_CONFIGURATION_PROPERTIES, .next=nullptr };
            CHECK_XRCMD(xrGetViewConfigurationProperties(m_instance, m_systemId, viewConfigType, &viewConfigProperties));

            Log::Write(Log::Level::Verbose,
                Fmt("  View configuration FovMutable=%s", viewConfigProperties.fovMutable == XR_TRUE ? "True" : "False"));

            uint32_t viewCount = 0;
            CHECK_XRCMD(xrEnumerateViewConfigurationViews(m_instance, m_systemId, viewConfigType, 0, &viewCount, nullptr));
            if (viewCount > 0) {
                std::vector<XrViewConfigurationView> views(viewCount, { .type=XR_TYPE_VIEW_CONFIGURATION_VIEW, .next=nullptr });
                CHECK_XRCMD(
                    xrEnumerateViewConfigurationViews(m_instance, m_systemId, viewConfigType, viewCount, &viewCount, views.data()));

                for (uint32_t i = 0; i < views.size(); ++i) {
                    const XrViewConfigurationView& view = views[i];

                    Log::Write(Log::Level::Verbose, Fmt("    View [%d]: Recommended Width=%d Height=%d SampleCount=%d", i,
                        view.recommendedImageRectWidth, view.recommendedImageRectHeight,
                        view.recommendedSwapchainSampleCount));
                    Log::Write(Log::Level::Verbose,
                        Fmt("    View [%d]:     Maximum Width=%d Height=%d SampleCount=%d", i, view.maxImageRectWidth,
                            view.maxImageRectHeight, view.maxSwapchainSampleCount));
                }
            }
            else {
                Log::Write(Log::Level::Error, Fmt("Empty view configuration type"));
            }
            LogEnvironmentBlendMode(viewConfigType);
        }
    }

    using XrEnvironmentBlendModeList = std::vector<XrEnvironmentBlendMode>;
    XrEnvironmentBlendModeList GetEnvironmentBlendModes(const XrViewConfigurationType type, const bool sortEntries = true) const
    {
        uint32_t count = 0;
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId, type, 0, &count, nullptr));
        if (count == 0)
            return {};
        std::vector<XrEnvironmentBlendMode> blendModes(count, XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId, type, count, &count, blendModes.data()));
        if (sortEntries) {
            std::sort(blendModes.begin(), blendModes.end());
        }
        return blendModes;
    }

    void LogEnvironmentBlendMode(XrViewConfigurationType type) {
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId != 0);

        const auto blendModes = GetEnvironmentBlendModes(type);
        Log::Write(Log::Level::Info, Fmt("Available Environment Blend Mode count : (%zu)", blendModes.size()));
        
        for (XrEnvironmentBlendMode mode : blendModes) {
            const bool blendModeMatch = (mode == m_environmentBlendMode);
            Log::Write(Log::Level::Info,
                Fmt("Environment Blend Mode (%s) : %s", to_string(mode), blendModeMatch ? "(Selected)" : ""));
        }
    }

    void InitializeSystem(const ALXR::ALXRPaths& alxrPaths) override {

        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId == XR_NULL_SYSTEM_ID);

        m_alxrPaths = alxrPaths;
        CHECK(m_alxrPaths != ALXR::ALXR_NULL_PATHS);

        m_formFactor = GetXrFormFactor(m_options->FormFactor);
        m_viewConfigType = GetXrViewConfigurationType(m_options->ViewConfiguration);
        m_environmentBlendMode = GetXrEnvironmentBlendMode(m_options->EnvironmentBlendMode);

        const XrSystemGetInfo systemInfo{
            .type = XR_TYPE_SYSTEM_GET_INFO,
            .next = nullptr,
            .formFactor = m_formFactor
        };        
        CHECK_XRCMD(xrGetSystem(m_instance, &systemInfo, &m_systemId));

        Log::Write(Log::Level::Verbose, Fmt("Using system %d for form factor %s", m_systemId, to_string(m_formFactor)));
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId != XR_NULL_SYSTEM_ID);

        LogViewConfigurations();

        m_availableBlendModes = GetEnvironmentBlendModes(m_viewConfigType, false);
        if (std::find(m_availableBlendModes.begin(), m_availableBlendModes.end(), m_environmentBlendMode) == m_availableBlendModes.end() && !m_availableBlendModes.empty()) {
            Log::Write(Log::Level::Info, Fmt
            (
                "Requested environment blend mode (%s) is not available, using first available mode (%s)",
                to_string(m_environmentBlendMode),
                to_string(m_availableBlendModes[0])
            ));
            m_environmentBlendMode = m_availableBlendModes[0];
        }

        if (!HasOpaqueEnvBlendMode()) {
            assert(m_environmentBlendMode.load() != XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
            m_currentPTMode.store(ALXR::PassthroughMode::BlendLayer);
        }
        Log::Write(Log::Level::Info, Fmt("Initial passthrough mode is %u", m_currentPTMode.load()));

        // The graphics API can initialize the graphics device now that the systemId and instance
        // handle are available.
        m_graphicsPlugin->InitializeDevice(m_instance, m_systemId, m_environmentBlendMode, IsExtEnabled(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME));
        m_isMultiViewEnabled = m_graphicsPlugin->IsMultiViewEnabled();
        
        Log::Write(Log::Level::Info, m_isMultiViewEnabled ?
            "Multi-view rendering enabled." :
            "Multi-view rendering not supported.");
    }

    inline std::vector<XrReferenceSpaceType> GetAvailableReferenceSpaces() const
    {
        CHECK(m_session != XR_NULL_HANDLE);
        uint32_t spaceCount = 0;
        CHECK_XRCMD(xrEnumerateReferenceSpaces(m_session, 0, &spaceCount, nullptr));
        assert(spaceCount > 0);
        std::vector<XrReferenceSpaceType> spaces(spaceCount);
        CHECK_XRCMD(xrEnumerateReferenceSpaces(m_session, spaceCount, &spaceCount, spaces.data()));
        return spaces;
    }

    inline XrReferenceSpaceCreateInfo GetAppReferenceSpaceCreateInfo() const {

        const auto appReferenceSpaceType = [this]() -> std::string_view
        {
            constexpr const auto refSpaceName = [](const XrReferenceSpaceType refType) {
                switch (refType) {
                case XR_REFERENCE_SPACE_TYPE_VIEW: return "View";
                case XR_REFERENCE_SPACE_TYPE_LOCAL: return "ALXRLocal";
                case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT: return "LocalFloor";
                case XR_REFERENCE_SPACE_TYPE_STAGE: return "Stage";
                case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT: return "UboundedMSFT";
                //case XR_REFERENCE_SPACE_TYPE_COMBINED_EYE_VARJO:
                };
                assert(false); // "Unknown HMD reference space type"
                return "Stage";
            };
            const auto availSpaces = GetAvailableReferenceSpaces();
            assert(availSpaces.size() > 0);
            // iterate through order of preference/priority, STAGE is the most preferred if available.
            for (const auto spaceType : {   XR_REFERENCE_SPACE_TYPE_STAGE,
                                            XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT,
                                            XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT,
                                            XR_REFERENCE_SPACE_TYPE_LOCAL,
                                            XR_REFERENCE_SPACE_TYPE_VIEW })
            {
                if (std::find(availSpaces.begin(), availSpaces.end(), spaceType) != availSpaces.end())
                    return refSpaceName(spaceType);
            }
            // should never reach this point.
            return refSpaceName(availSpaces[0]);
        }();
        return GetXrReferenceSpaceCreateInfo(appReferenceSpaceType);
    }

#ifdef XR_USE_PLATFORM_WIN32
    static inline std::int64_t ToTimeNs(const LARGE_INTEGER& ctr)
    {
        const std::int64_t freq = _Query_perf_frequency(); // doesn't change after system boot
        const std::int64_t whole = (ctr.QuadPart / freq) * std::nano::den;
        const std::int64_t part = (ctr.QuadPart % freq) * std::nano::den / freq;
        return (whole + part);
    }
#else
    static inline std::int64_t ToTimeNs(const struct timespec& ts)
    {
        return (ts.tv_sec * 1000000000ll) + ts.tv_nsec;
    }
#endif

    inline std::int64_t FromXrTimeNs(const XrTime xrt) const
    {
#ifdef XR_USE_PLATFORM_WIN32
        if (m_pfnConvertTimeToWin32PerformanceCounterKHR == nullptr)
            return static_cast<std::int64_t>(xrt);
        LARGE_INTEGER ctr;
        if (XR_FAILED(m_pfnConvertTimeToWin32PerformanceCounterKHR(m_instance, xrt, &ctr)))
            return static_cast<std::int64_t>(xrt);
        return ToTimeNs(ctr);
#else
        if (m_pfnConvertTimeToTimespecTimeKHR == nullptr)
            return static_cast<std::int64_t>(xrt);
        struct timespec ts;
        if (XR_FAILED(m_pfnConvertTimeToTimespecTimeKHR(m_instance, xrt, &ts)))
            return static_cast<std::int64_t>(xrt);
        return ToTimeNs(ts);
#endif
    }

    virtual inline std::tuple<XrTime, std::int64_t> XrTimeNow() const override
    {
        static_assert(sizeof(XrTime) == sizeof(std::int64_t) && std::is_signed<XrTime>::value);
#ifdef XR_USE_PLATFORM_WIN32
        LARGE_INTEGER ctr;
        QueryPerformanceCounter(&ctr);
        const std::int64_t ctrNs = ToTimeNs(ctr);
        if (m_pfnConvertWin32PerformanceCounterToTimeKHR == nullptr)
            return { static_cast<XrTime>(ctrNs), ctrNs };
        XrTime xrTimeNow;
        if (XR_FAILED(m_pfnConvertWin32PerformanceCounterToTimeKHR(m_instance, &ctr, &xrTimeNow)))
            return { static_cast<XrTime>(ctrNs), ctrNs };
        return { xrTimeNow, ctrNs };
#else
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
            return { 0,0 };
        const std::int64_t tsNs = ToTimeNs(ts);
        if (IsPrePicoPUI<5,4>() || m_pfnConvertTimespecTimeToTimeKHR == nullptr) {
            // 
            // There are bugs in Pico's OXR runtime in firmware versions < v5.4 with either/both:
            //      * xrLocateSpace for controller action spaces not working with any other times beyond XrFrameState::predicateDisplayTime (and zero, in a non-conforming way).
            //      * xrConvertTimeToTimespecTimeKHR appears to return values in microseconds instead of nanoseconds and values seem to be completely off from what
            //        XrFrameState::predicateDisplayTime values are.   
            //
            return { static_cast<XrTime>(tsNs), tsNs };
        }
        XrTime xrTimeNow;
        if (XR_FAILED(m_pfnConvertTimespecTimeToTimeKHR(m_instance, &ts, &xrTimeNow)))
            return { static_cast<XrTime>(tsNs), tsNs };
        return { xrTimeNow, tsNs };
#endif
    }

    void LogReferenceSpaces() {
        CHECK(m_session != XR_NULL_HANDLE);

        const auto spaces = GetAvailableReferenceSpaces();
        Log::Write(Log::Level::Info, Fmt("Available reference spaces: %d", spaces.size()));
        for (const XrReferenceSpaceType space : spaces) {
            Log::Write(Log::Level::Verbose, Fmt("  Name: %s", to_string(space)));
        }
    }

    void InitializeActions() {
        CHECK(m_instance != XR_NULL_HANDLE);

        const auto IsProfileSupported = [this](const auto& profile)
        {
            if (&profile == &ALXR::EyeGazeProfile)
                return IsExtEyeGazeInteractionSupported();

            if (m_options && m_options->DisableSuggestedBindings)
                return false;
            if (IsRuntime(OxrRuntimeType::HTCWave)) {
                if (profile.IsCore())
                    return false;
                constexpr const std::array<const std::string_view, 2> HTCFilterList{
                    XR_HTC_VIVE_FOCUS3_CONTROLLER_INTERACTION_EXTENSION_NAME,
                    XR_HTC_HAND_INTERACTION_EXTENSION_NAME
                };
                for (const auto& htcProfile : HTCFilterList) {
                    if (htcProfile == profile.extensionName)
                        return IsExtEnabled(profile.extensionName);
                }
                return false;
            }
            return profile.IsCore() || IsExtEnabled(profile.extensionName);
        };
        m_interactionManager = std::make_unique<ALXR::InteractionManager>
        (
            m_instance,
            m_session,
            m_alxrPaths,
            IsPassthroughSupported() ?
                [this](const ALXR::PassthroughMode newMode) { TogglePassthroughMode(newMode); } : ALXR::TogglePTModeFn {},          
            IsProfileSupported
        );
    }

    inline bool IsExtEnabled(const std::string_view& extName) const
    {
        auto ext_itr = m_availableSupportedExtMap.find(extName);
        return ext_itr != m_availableSupportedExtMap.end() && ext_itr->second;
    }

    bool InitializeExtensions()
    {
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_session != XR_NULL_HANDLE);

#ifdef XR_USE_PLATFORM_WIN32
        if (IsExtEnabled(XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME))
        {
            Log::Write(Log::Level::Info, Fmt("%s enabled.", XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME));
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xrConvertTimeToWin32PerformanceCounterKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnConvertTimeToWin32PerformanceCounterKHR)));
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xrConvertWin32PerformanceCounterToTimeKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnConvertWin32PerformanceCounterToTimeKHR)));
        }
#endif
        if (IsExtEnabled(XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME))
        {
            Log::Write(Log::Level::Info, Fmt("%s enabled.", XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME));
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xrConvertTimespecTimeToTimeKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnConvertTimespecTimeToTimeKHR)));
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xrConvertTimeToTimespecTimeKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnConvertTimeToTimespecTimeKHR)));
        }

        if (IsExtEnabled(XR_FB_COLOR_SPACE_EXTENSION_NAME))
        {
            Log::Write(Log::Level::Info, Fmt("%s enabled.", XR_FB_COLOR_SPACE_EXTENSION_NAME));
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xrEnumerateColorSpacesFB",
                reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnEnumerateColorSpacesFB)));
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xrSetColorSpaceFB",
                reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnSetColorSpaceFB)));
        }

        if (IsExtEnabled(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
        {
            Log::Write(Log::Level::Info, Fmt("%s enabled.", XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME));
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xrEnumerateDisplayRefreshRatesFB",
                reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnEnumerateDisplayRefreshRatesFB)));
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xrGetDisplayRefreshRateFB",
                reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnGetDisplayRefreshRateFB)));
            CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xrRequestDisplayRefreshRateFB",
                reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnRequestDisplayRefreshRateFB)));
        }

#ifdef XR_USE_OXR_PICO
        const auto GetPicoInstanceProcAddr = [this](const char* const name, auto& fn)
        {
            const XrResult result = xrGetInstanceProcAddr(m_instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&fn));
            if (result != XR_SUCCESS) {
                Log::Write(Log::Level::Warning, Fmt("Unable to load xr-extension function: %s, error-code: %d", name, result));
            }
        };

        if (IsExtEnabled("XR_PICO_boundary") || IsExtEnabled(XR_PICO_BOUNDARY_EXT_EXTENSION_NAME)) {
            Log::Write(Log::Level::Info, Fmt("%s enabled.", "XR_PICO_boundary(_ext)"));
            GetPicoInstanceProcAddr("xrInvokeFunctionsPICO", m_pfnInvokeFunctionsPICO);
        }
#endif

        SetDeviceColorSpace();
        UpdateSupportedDisplayRefreshRates();
        InitializePassthroughAPI();
        InitializeEyeTrackers();
        InitializeFacialTracker();
        InitializeProxyServer();
        return InitializeHandTrackers();
    }

    bool SetDeviceColorSpace()
    {
        if (m_pfnSetColorSpaceFB == nullptr)
            return false;

        constexpr const auto to_string = [](const XrColorSpaceFB csType) -> const char*
        {
            switch (csType)
            {
            case XR_COLOR_SPACE_UNMANAGED_FB: return "UNMANAGED";
            case XR_COLOR_SPACE_REC2020_FB: return "REC2020";
            case XR_COLOR_SPACE_REC709_FB: return "REC709";
            case XR_COLOR_SPACE_RIFT_CV1_FB: return "RIFT_CV1";
            case XR_COLOR_SPACE_RIFT_S_FB: return "RIFT_S";
            case XR_COLOR_SPACE_QUEST_FB: return "QUEST";
            case XR_COLOR_SPACE_P3_FB: return "P3";
            case XR_COLOR_SPACE_ADOBE_RGB_FB: return "ADOBE_RGB";
            }
            return "unknown-color-space-type";
        };

        //std::uint32_t colorSpaceCount = 0;
        //CHECK_XRCMD(m_pfnEnumerateColorSpacesFB(m_session, 0, &colorSpaceCount, nullptr));

        //std::vector<XrColorSpaceFB> colorSpaceTypes{ colorSpaceCount, XR_COLOR_SPACE_UNMANAGED_FB };
        //CHECK_XRCMD(m_pfnEnumerateColorSpacesFB(m_session, colorSpaceCount, &colorSpaceCount, colorSpaceTypes.data()));

        const XrColorSpaceFB selectedColorSpace = m_options ?
            m_options->DisplayColorSpace : static_cast<XrColorSpaceFB>(ALXRColorSpace::Default);
        const auto colorSpaceName = to_string(selectedColorSpace);

        if (m_pfnSetColorSpaceFB(m_session, selectedColorSpace) != XR_SUCCESS) {
            Log::Write(Log::Level::Warning, Fmt("Failed to set display colour space to \"%s\"", colorSpaceName));
            return false;
        }
        Log::Write(Log::Level::Info, Fmt("Color space set successefully set to \"%s\"", colorSpaceName));
        return true;
    }

    XrEyeTrackerANDROID m_eyeTrackerANDROID{ XR_NULL_HANDLE };
    PFN_xrDestroyEyeTrackerANDROID xrDestroyEyeTrackerANDROID{ nullptr };
    PFN_xrGetEyesInfoANDROID xrGetEyesInfoANDROID{nullptr};

    bool InitializeAndroidAvatarEyesTracker()
    {
        if (!IsExtEnabled(XR_ANDROID_AVATAR_EYES_EXTENSION_NAME) ||
            (m_options && !m_options->IsSelected(ALXREyeTrackingType::AndroidAvatarEyes))) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_ANDROID_AVATAR_EYES_EXTENSION_NAME));
            return false;
        }

        XrSystemAvatarEyesPropertiesANDROID eyeTrackingSystemProperties{
            .type = XR_TYPE_SYSTEM_AVATAR_EYES_PROPERTIES_ANDROID,
            .next = nullptr,
            .supportsAvatarEyes = XR_FALSE
        };
        XrSystemProperties systemProperties{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &eyeTrackingSystemProperties
        };
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &systemProperties)) ||
            !eyeTrackingSystemProperties.supportsAvatarEyes) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_ANDROID_AVATAR_EYES_EXTENSION_NAME));
            return false;
        }

        PFN_xrCreateEyeTrackerANDROID xrCreateEyeTrackerANDROID = nullptr;

        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance, "xrCreateEyeTrackerANDROID", (PFN_xrVoidFunction*)(&xrCreateEyeTrackerANDROID)))) {
            xrCreateEyeTrackerANDROID = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrDestroyEyeTrackerANDROID",
            (PFN_xrVoidFunction*)(&xrDestroyEyeTrackerANDROID)))) {
            xrDestroyEyeTrackerANDROID = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance, "xrGetEyesInfoANDROID", (PFN_xrVoidFunction*)(&xrGetEyesInfoANDROID)))) {
            xrGetEyesInfoANDROID = nullptr;
        }

        if (xrCreateEyeTrackerANDROID == nullptr ||
            xrDestroyEyeTrackerANDROID == nullptr ||
            xrGetEyesInfoANDROID == nullptr) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_ANDROID_AVATAR_EYES_EXTENSION_NAME));
            return false;
        }

        Log::Write(Log::Level::Info, Fmt("%s is enabled.", XR_ANDROID_AVATAR_EYES_EXTENSION_NAME));

        // Create Eye Tracker
        constexpr const XrEyeTrackerCreateInfoANDROID createInfo = {
            .type = XR_TYPE_EYE_TRACKER_CREATE_INFO_ANDROID,
            .next = nullptr
        };
        return XR_SUCCEEDED(xrCreateEyeTrackerANDROID(m_session, &createInfo, &m_eyeTrackerANDROID)) && m_eyeTrackerANDROID != XR_NULL_HANDLE;
    }

    XrEyeTrackerFB eyeTrackerFB_ = XR_NULL_HANDLE;
    PFN_xrDestroyEyeTrackerFB m_xrDestroyEyeTrackerFB_ = nullptr;
    PFN_xrGetEyeGazesFB m_xrGetEyeGazesFB_ = nullptr;

    bool InitializeFBEyeTrackers()
    {
        if (!IsExtEnabled(XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME) ||
            (m_options && !m_options->IsSelected(ALXREyeTrackingType::FBEyeTrackingSocial))) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME));
            return false;
        }

        XrSystemEyeTrackingPropertiesFB eyeTrackingSystemProperties{
            .type = XR_TYPE_SYSTEM_EYE_TRACKING_PROPERTIES_FB,
            .next = nullptr,
            .supportsEyeTracking = XR_FALSE
        };
        XrSystemProperties systemProperties{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &eyeTrackingSystemProperties
        };
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &systemProperties)) ||
            !eyeTrackingSystemProperties.supportsEyeTracking) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME));
            return false;
        }

        // Acquire Function Pointers
        PFN_xrCreateEyeTrackerFB m_xrCreateEyeTrackerFB_ = nullptr;

        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance, "xrCreateEyeTrackerFB", (PFN_xrVoidFunction*)(&m_xrCreateEyeTrackerFB_)))) {
            m_xrCreateEyeTrackerFB_ = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrDestroyEyeTrackerFB",
            (PFN_xrVoidFunction*)(&m_xrDestroyEyeTrackerFB_)))) {
            m_xrDestroyEyeTrackerFB_ = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance, "xrGetEyeGazesFB", (PFN_xrVoidFunction*)(&m_xrGetEyeGazesFB_)))) {
            m_xrGetEyeGazesFB_ = nullptr;
        }

        if (m_xrCreateEyeTrackerFB_ == nullptr ||
            m_xrDestroyEyeTrackerFB_ == nullptr ||
            m_xrGetEyeGazesFB_ == nullptr) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME));
            return false;
        }

        Log::Write(Log::Level::Info, Fmt("%s is enabled.", XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME));

        // Create Eye Tracker
        constexpr const XrEyeTrackerCreateInfoFB createInfo {
            .type = XR_TYPE_EYE_TRACKER_CREATE_INFO_FB,
            .next = nullptr
        };
        return XR_SUCCEEDED(m_xrCreateEyeTrackerFB_(m_session, &createInfo, &eyeTrackerFB_)) && eyeTrackerFB_ != XR_NULL_HANDLE;
    }

    bool IsExtEyeGazeInteractionSupported() const {
        if (!IsExtEnabled(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) ||
            (m_options && !m_options->IsSelected(ALXREyeTrackingType::ExtEyeGazeInteraction)))
        {
            return false;
        }
        XrSystemEyeGazeInteractionPropertiesEXT eyeTrackingSystemProperties{
            .type = XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT,
            .next = nullptr,
            .supportsEyeGazeInteraction = XR_FALSE
        };
        XrSystemProperties systemProperties{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &eyeTrackingSystemProperties
        };
        return XR_SUCCEEDED(xrGetSystemProperties(m_instance, m_systemId, &systemProperties)) &&
               eyeTrackingSystemProperties.supportsEyeGazeInteraction == XR_TRUE;
    }

    bool InitExtEyeGazeInteraction() {        
        if (!IsExtEyeGazeInteractionSupported()) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME));
            return false;
        }
        Log::Write(Log::Level::Info, Fmt("%s is enabled.", XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME));
        return true;
    }

    bool InitializeEyeTrackers() {
        if (InitializeFBEyeTrackers())
            return true;
        if (InitializeAndroidAvatarEyesTracker())
            return true;
        return InitExtEyeGazeInteraction();
    }

    bool IsEyeTrackingEnabled() const override {
        if (m_eyeTrackerANDROID != XR_NULL_HANDLE || eyeTrackerFB_ != XR_NULL_HANDLE)
            return true;
        return IsExtEyeGazeInteractionSupported();
    }

    XrFaceTrackerANDROID m_faceTrackerANDROID{ XR_NULL_HANDLE };
    PFN_xrDestroyFaceTrackerANDROID xrDestroyFaceTrackerANDROID{ nullptr };
    PFN_xrGetFaceStateANDROID xrGetFaceStateANDROID{ nullptr };

    bool InitializeAndroidFacialTracker() // XR_ANDROID_face_tracking
    {
        if (!IsExtEnabled(XR_ANDROID_FACE_TRACKING_EXTENSION_NAME) ||
            (m_options && !m_options->IsSelected(ALXRFacialExpressionType::Android))) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_ANDROID_FACE_TRACKING_EXTENSION_NAME));
            return false;
        }

        XrSystemFaceTrackingPropertiesANDROID faceTrackingSystemProperties = {
            .type = XR_TYPE_SYSTEM_FACE_TRACKING_PROPERTIES_ANDROID,
            .next = nullptr,
            .supportsFaceTracking = XR_FALSE
        };
        XrSystemProperties systemProperties = {
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &faceTrackingSystemProperties
        };
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &systemProperties)) ||
            !faceTrackingSystemProperties.supportsFaceTracking) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_ANDROID_FACE_TRACKING_EXTENSION_NAME));
            return false;
        }

        // Acquire Function Pointers
        PFN_xrCreateFaceTrackerANDROID xrCreateFaceTrackerANDROID = nullptr;
        PFN_xrGetFaceCalibrationStateANDROID xrGetFaceCalibrationStateANDROID = nullptr;

        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrCreateFaceTrackerANDROID",
            (PFN_xrVoidFunction*)(&xrCreateFaceTrackerANDROID)))) {
            xrCreateFaceTrackerANDROID = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrDestroyFaceTrackerANDROID",
            (PFN_xrVoidFunction*)(&xrDestroyFaceTrackerANDROID)))) {
            xrDestroyFaceTrackerANDROID = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrGetFaceStateANDROID",
            (PFN_xrVoidFunction*)(&xrGetFaceStateANDROID)))) {
            xrGetFaceStateANDROID = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrGetFaceCalibrationStateANDROID",
            (PFN_xrVoidFunction*)(&xrGetFaceCalibrationStateANDROID)))) {
            xrGetFaceCalibrationStateANDROID = nullptr;
        }

        if (xrCreateFaceTrackerANDROID == nullptr ||
            xrDestroyFaceTrackerANDROID == nullptr ||
            xrGetFaceStateANDROID == nullptr) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_ANDROID_FACE_TRACKING_EXTENSION_NAME));
            return false;
        }

        Log::Write(Log::Level::Info, Fmt("%s is enabled.", XR_ANDROID_FACE_TRACKING_EXTENSION_NAME));

        m_faceTrackerANDROID = XR_NULL_HANDLE;
        constexpr const XrFaceTrackerCreateInfoANDROID createInfo = {
            .type = XR_TYPE_FACE_TRACKER_CREATE_INFO_ANDROID,
            .next = nullptr,
        };
        if (XR_FAILED(xrCreateFaceTrackerANDROID(m_session, &createInfo, &m_faceTrackerANDROID)) ||
            m_faceTrackerANDROID == XR_NULL_HANDLE) {
            return false;
        }

        if (xrGetFaceCalibrationStateANDROID) {
            XrBool32 isCalibrated = XR_FALSE;
            if (XR_FAILED(xrGetFaceCalibrationStateANDROID(m_faceTrackerANDROID, &isCalibrated)) || isCalibrated == XR_FALSE) {
                Log::Write(Log::Level::Warning, Fmt("Face tracking has not been calibrated, please go to system sets and calibrate"));
            }
        }

        return true;
    }

    XrFaceTracker2FB faceTrackerFBV2_ = XR_NULL_HANDLE;
    PFN_xrDestroyFaceTracker2FB m_xrDestroyFaceTracker2FB_ = nullptr;
    PFN_xrGetFaceExpressionWeights2FB m_xrGetFaceExpressionWeights2FB_ = nullptr;

    bool InitializeFBFacialTrackerV2()
    {
        if (!IsExtEnabled(XR_FB_FACE_TRACKING2_EXTENSION_NAME) ||
            (m_options && !m_options->IsSelected(ALXRFacialExpressionType::FB_V2))) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_FACE_TRACKING2_EXTENSION_NAME));
            return false;
        }

        XrSystemFaceTrackingProperties2FB faceTrackingSystemProperties{
            .type = XR_TYPE_SYSTEM_FACE_TRACKING_PROPERTIES2_FB,
            .next = nullptr,
            .supportsVisualFaceTracking = XR_FALSE,
            .supportsAudioFaceTracking = XR_FALSE,
        };
        XrSystemProperties systemProperties{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &faceTrackingSystemProperties
        };
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &systemProperties)) ||
            (!faceTrackingSystemProperties.supportsVisualFaceTracking &&
             !faceTrackingSystemProperties.supportsAudioFaceTracking)) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_FACE_TRACKING2_EXTENSION_NAME));
            return false;
        }

        // Acquire Function Pointers
        PFN_xrCreateFaceTracker2FB m_xrCreateFaceTrackerFB_ = nullptr;

        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrCreateFaceTracker2FB",
            (PFN_xrVoidFunction*)(&m_xrCreateFaceTrackerFB_)))) {
            m_xrDestroyFaceTracker2FB_ = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrDestroyFaceTracker2FB",
            (PFN_xrVoidFunction*)(&m_xrDestroyFaceTracker2FB_)))) {
            m_xrDestroyFaceTracker2FB_ = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrGetFaceExpressionWeights2FB",
            (PFN_xrVoidFunction*)(&m_xrGetFaceExpressionWeights2FB_)))) {
            m_xrGetFaceExpressionWeights2FB_ = nullptr;
        }

        if (m_xrCreateFaceTrackerFB_ == nullptr ||
            m_xrDestroyFaceTracker2FB_ == nullptr ||
            m_xrGetFaceExpressionWeights2FB_ == nullptr) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_FACE_TRACKING2_EXTENSION_NAME));
            return false;
        }

        Log::Write(Log::Level::Info, Fmt("%s is enabled.", XR_FB_FACE_TRACKING2_EXTENSION_NAME));

        Log::Write(Log::Level::Info, Fmt("Runtime supports the following data sources for %s:", XR_FB_FACE_TRACKING2_EXTENSION_NAME));
        Log::Write(Log::Level::Info, Fmt("\tVisual Face Tracking: %s", faceTrackingSystemProperties.supportsVisualFaceTracking ? "true" : "false"));
        Log::Write(Log::Level::Info, Fmt("\t Audio Face Tracking: %s", faceTrackingSystemProperties.supportsAudioFaceTracking  ? "true" : "false"));

        std::vector<XrFaceTrackingDataSource2FB> dataSources{};
        dataSources.reserve(2);
        if (faceTrackingSystemProperties.supportsVisualFaceTracking == XR_TRUE &&
            (m_options == nullptr || m_options->IsFaceTrackingSourceEnabled(ALXR_FACE_TRACKING_DATA_SOURCE_VISUAL))) {
            dataSources.push_back(XR_FACE_TRACKING_DATA_SOURCE2_VISUAL_FB);
            Log::Write(Log::Level::Info, Fmt("Visual data sources selected for %s", XR_FB_FACE_TRACKING2_EXTENSION_NAME));
        }
        if (faceTrackingSystemProperties.supportsAudioFaceTracking == XR_TRUE &&
            (m_options == nullptr || m_options->IsFaceTrackingSourceEnabled(ALXR_FACE_TRACKING_DATA_SOURCE_AUDIO))) {
            dataSources.push_back(XR_FACE_TRACKING_DATA_SOURCE2_AUDIO_FB);
            Log::Write(Log::Level::Info, Fmt("Audio data sources selected for %s", XR_FB_FACE_TRACKING2_EXTENSION_NAME));
        }

        if (dataSources.empty()) {
			Log::Write(Log::Level::Warning, "No face tracking data sources selected, face tracker will not be created.");
			return false;
		}

        faceTrackerFBV2_ = XR_NULL_HANDLE;
        const XrFaceTrackerCreateInfo2FB createInfo{
            .type = XR_TYPE_FACE_TRACKER_CREATE_INFO2_FB,
            .next = nullptr,
            .faceExpressionSet = XR_FACE_EXPRESSION_SET2_DEFAULT_FB,
            .requestedDataSourceCount = (std::uint32_t)dataSources.size(),
            .requestedDataSources = dataSources.data(),
        };
        return XR_SUCCEEDED(m_xrCreateFaceTrackerFB_(m_session, &createInfo, &faceTrackerFBV2_)) && faceTrackerFBV2_ != XR_NULL_HANDLE;
    }

    XrFaceTrackerFB faceTrackerFB_ = XR_NULL_HANDLE;
    PFN_xrDestroyFaceTrackerFB m_xrDestroyFaceTrackerFB_ = nullptr;
    PFN_xrGetFaceExpressionWeightsFB m_xrGetFaceExpressionWeightsFB_ = nullptr;

    bool InitializeFBFacialTracker()
    {
        if (!IsExtEnabled(XR_FB_FACE_TRACKING_EXTENSION_NAME) ||
            (m_options && !m_options->IsSelected(ALXRFacialExpressionType::FB))) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_FACE_TRACKING_EXTENSION_NAME));
            return false;
        }

        XrSystemFaceTrackingPropertiesFB faceTrackingSystemProperties{
            .type = XR_TYPE_SYSTEM_FACE_TRACKING_PROPERTIES_FB,
            .next = nullptr,
            .supportsFaceTracking = XR_FALSE
        };
        XrSystemProperties systemProperties{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &faceTrackingSystemProperties
        };
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &systemProperties)) ||
            !faceTrackingSystemProperties.supportsFaceTracking) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_FACE_TRACKING_EXTENSION_NAME));
            return false;
        }

        // Acquire Function Pointers
        PFN_xrCreateFaceTrackerFB m_xrCreateFaceTrackerFB_ = nullptr;

        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrCreateFaceTrackerFB",
            (PFN_xrVoidFunction*)(&m_xrCreateFaceTrackerFB_)))) {
            m_xrCreateFaceTrackerFB_ = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrDestroyFaceTrackerFB",
            (PFN_xrVoidFunction*)(&m_xrDestroyFaceTrackerFB_)))) {
            m_xrDestroyFaceTrackerFB_ = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrGetFaceExpressionWeightsFB",
            (PFN_xrVoidFunction*)(&m_xrGetFaceExpressionWeightsFB_)))) {
            m_xrGetFaceExpressionWeightsFB_ = nullptr;
        }

        if (m_xrCreateFaceTrackerFB_ == nullptr ||
            m_xrDestroyFaceTrackerFB_ == nullptr ||
            m_xrGetFaceExpressionWeightsFB_ == nullptr) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_FACE_TRACKING_EXTENSION_NAME));
            return false;
        }

        Log::Write(Log::Level::Info, Fmt("%s is enabled.", XR_FB_FACE_TRACKING_EXTENSION_NAME));
        
        faceTrackerFB_ = XR_NULL_HANDLE;
        constexpr const XrFaceTrackerCreateInfoFB createInfo {
            .type = XR_TYPE_FACE_TRACKER_CREATE_INFO_FB,
            .next = nullptr,
            .faceExpressionSet = XR_FACE_EXPRESSSION_SET_DEFAULT_FB
        };
        return XR_SUCCEEDED(m_xrCreateFaceTrackerFB_(m_session, &createInfo, &faceTrackerFB_)) && faceTrackerFB_ != XR_NULL_HANDLE;
    }

    std::array<XrFacialTrackerHTC, 2> m_facialTrackersHTC{ XR_NULL_HANDLE, XR_NULL_HANDLE };
    PFN_xrDestroyFacialTrackerHTC m_xrDestroyFacialTrackerHTC = nullptr;
    PFN_xrGetFacialExpressionsHTC m_xrGetFacialExpressionsHTC = nullptr;

    bool InitializeHTCFacialTracker() {

        if (!IsExtEnabled(XR_HTC_FACIAL_TRACKING_EXTENSION_NAME) ||
            (m_options && !m_options->IsSelected(ALXRFacialExpressionType::HTC)))
        {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_HTC_FACIAL_TRACKING_EXTENSION_NAME));
            return false;
        }

        XrSystemFacialTrackingPropertiesHTC faceTrackingSystemProperties{
            .type = XR_TYPE_SYSTEM_FACIAL_TRACKING_PROPERTIES_HTC,
            .next = nullptr,
            .supportEyeFacialTracking = XR_FALSE,
            .supportLipFacialTracking = XR_FALSE
        };
        XrSystemProperties systemProperties{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &faceTrackingSystemProperties
        };
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &systemProperties)) ||
            !(faceTrackingSystemProperties.supportEyeFacialTracking ||
              faceTrackingSystemProperties.supportLipFacialTracking)) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_HTC_FACIAL_TRACKING_EXTENSION_NAME));
            return false;
        }

        PFN_xrCreateFacialTrackerHTC m_xrCreateFaceTrackerHTC = nullptr;

        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrCreateFacialTrackerHTC",
            (PFN_xrVoidFunction*)(&m_xrCreateFaceTrackerHTC)))) {
            m_xrCreateFaceTrackerHTC = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrDestroyFacialTrackerHTC",
            (PFN_xrVoidFunction*)(&m_xrDestroyFacialTrackerHTC)))) {
            m_xrDestroyFacialTrackerHTC = nullptr;
        }
        if (XR_FAILED(xrGetInstanceProcAddr(
            m_instance,
            "xrGetFacialExpressionsHTC",
            (PFN_xrVoidFunction*)(&m_xrGetFacialExpressionsHTC)))) {
            m_xrGetFacialExpressionsHTC = nullptr;
        }

        if (m_xrCreateFaceTrackerHTC == nullptr ||
            m_xrDestroyFacialTrackerHTC == nullptr ||
            m_xrGetFacialExpressionsHTC == nullptr) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_HTC_FACIAL_TRACKING_EXTENSION_NAME));
            return false;
        }

        Log::Write(Log::Level::Info, Fmt("%s is enabled.", XR_HTC_FACIAL_TRACKING_EXTENSION_NAME));

        assert(m_facialTrackersHTC.size() == 2);
        auto facialTrackerPtr = m_facialTrackersHTC.data();
        for (const auto& [isTrackingTypeSupported, facialTrackingType] : {
            std::make_tuple(faceTrackingSystemProperties.supportEyeFacialTracking, XR_FACIAL_TRACKING_TYPE_EYE_DEFAULT_HTC),
            std::make_tuple(faceTrackingSystemProperties.supportLipFacialTracking, XR_FACIAL_TRACKING_TYPE_LIP_DEFAULT_HTC)
        }) {
            if (isTrackingTypeSupported) {
                CHECK(*facialTrackerPtr == XR_NULL_HANDLE);
                const XrFacialTrackerCreateInfoHTC  createInfo{
                    .type = XR_TYPE_FACIAL_TRACKER_CREATE_INFO_HTC,
                    .next = nullptr,
                    .facialTrackingType = facialTrackingType
                };
                if (XR_FAILED(m_xrCreateFaceTrackerHTC(m_session, &createInfo, facialTrackerPtr))) {
                    *facialTrackerPtr = XR_NULL_HANDLE;
                    Log::Write(Log::Level::Error, Fmt("Failed to create XrFacialTrackerHTC of facial tracking type: %u", facialTrackingType));
                }
                else {
                    CHECK(*facialTrackerPtr != XR_NULL_HANDLE);
                }
            }
            ++facialTrackerPtr;
        }
        return std::any_of
        (
            m_facialTrackersHTC.begin(), m_facialTrackersHTC.end(),
            [](auto facialTrackerPtr) { return facialTrackerPtr != XR_NULL_HANDLE; }
        );
    }

    bool InitializeFacialTracker() {
        if (InitializeFBFacialTrackerV2())
            return true;
        if (InitializeFBFacialTracker())
            return true;
        if (InitializeHTCFacialTracker())
            return true;
        return InitializeAndroidFacialTracker();
    }

    bool IsFacialTrackingEnabled() const override {
        if (faceTrackerFBV2_ != XR_NULL_HANDLE)
            return true;
        if (faceTrackerFB_ != XR_NULL_HANDLE)
            return true;
        if (m_faceTrackerANDROID != XR_NULL_HANDLE)
            return true;
        return std::any_of(
            m_facialTrackersHTC.begin(), m_facialTrackersHTC.end(),
            [](const auto facialTrackerPtr) { return facialTrackerPtr != XR_NULL_HANDLE; }
        );
    }

    std::unique_ptr<ALXR::VRCFT::Server> m_vrcftProxyServer{};
    
    bool InitializeProxyServer()
    {
        if (m_options && m_options->NoFTServer) {
            Log::Write(Log::Level::Info,
                "\"no_tracking_server\" option is enabled, tracking proxy server will not be created. "
                "No third-party/external apps such as VRCFT will be able to connect.");
            return false;
        }

        if (!IsEyeTrackingEnabled() && !IsFacialTrackingEnabled()) {
            Log::Write(Log::Level::Warning, "No Facial or Eye Tracking enabled/supported, FacialEye Tracking proxy server not created.");
            return false;
        }

        const std::uint16_t portNo = m_options != nullptr ?
            m_options->TrackingServerPortNo : ALXR::VRCFT::Server::DefaultPortNo;
        m_vrcftProxyServer = std::make_unique<ALXR::VRCFT::Server>(portNo);
        assert(m_vrcftProxyServer != nullptr);
        Log::Write(Log::Level::Info, "FacialEye Tracking proxy server created.");
        return true;
    }

    bool InitializeHandTrackers()
    {
        assert(m_instance != XR_NULL_HANDLE);
        assert(m_session != XR_NULL_HANDLE);

        if (!IsExtEnabled(XR_EXT_HAND_TRACKING_EXTENSION_NAME) ||
            (m_options && m_options->NoHandTracking)) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_EXT_HAND_TRACKING_EXTENSION_NAME));
            return false;
        }
        
        // Inspect hand tracking system properties
        XrSystemHandTrackingPropertiesEXT handTrackingSystemProperties{ 
            .type = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT,
            .next = nullptr,
            .supportsHandTracking = XR_FALSE
        };
        XrSystemProperties systemProperties{ .type=XR_TYPE_SYSTEM_PROPERTIES, .next = &handTrackingSystemProperties };
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &systemProperties)) ||
            handTrackingSystemProperties.supportsHandTracking == XR_FALSE) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_EXT_HAND_TRACKING_EXTENSION_NAME));
            // The system does not support hand tracking
            return false;
        }

        // Get function pointer for xrCreateHandTrackerEXT
        if (XR_FAILED(xrGetInstanceProcAddr(m_instance, "xrCreateHandTrackerEXT",
            reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnCreateHandTrackerEXT)))) {
            m_pfnCreateHandTrackerEXT = nullptr;
        }

        // Get function pointer for xrLocateHandJointsEXT
        if (XR_FAILED(xrGetInstanceProcAddr(m_instance, "xrLocateHandJointsEXT",
            reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnLocateHandJointsEXT)))) {
            m_pfnLocateHandJointsEXT = nullptr;
        }

        // Get function pointer for xrLocateHandJointsEXT
        if (XR_FAILED(xrGetInstanceProcAddr(m_instance, "xrDestroyHandTrackerEXT",
            reinterpret_cast<PFN_xrVoidFunction*>(&m_pfnDestroyHandTrackerEXT)))) {
            m_pfnDestroyHandTrackerEXT = nullptr;
        }

        if (m_pfnCreateHandTrackerEXT == nullptr ||
            m_pfnLocateHandJointsEXT == nullptr ||
            m_pfnDestroyHandTrackerEXT == nullptr)
        {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_EXT_HAND_TRACKING_EXTENSION_NAME));
            return false;
        }
        Log::Write(Log::Level::Info, Fmt("%s is enabled.", XR_EXT_HAND_TRACKING_EXTENSION_NAME));

        // Create a hand tracker for left hand that tracks default set of hand joints.
        const auto createHandTracker = [&](auto& handTracker, const XrHandEXT hand)
        {
            const XrHandTrackerCreateInfoEXT createInfo{
                .type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
                .next = nullptr,
                .hand = hand,
                .handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT
            };
            if (XR_FAILED(m_pfnCreateHandTrackerEXT(m_session, &createInfo, &handTracker.tracker))) {
                handTracker.tracker = XR_NULL_HANDLE;
                Log::Write(Log::Level::Error, Fmt("Failed to create hand tracker for %s hand.", hand == XR_HAND_LEFT_EXT ? "left" : "right"));
            }
        };
        createHandTracker(m_input.handTrackers[0], XR_HAND_LEFT_EXT);
        createHandTracker(m_input.handTrackers[1], XR_HAND_RIGHT_EXT);

        auto& leftHandBaseOrientation = m_input.handTrackers[0].baseOrientation;
        auto& rightHandBaseOrientation = m_input.handTrackers[1].baseOrientation;
        auto& yRot = rightHandBaseOrientation;
        yRot = Eigen::AngleAxisf(ALXR::ToRadians(-90.0f), Eigen::Vector3f(0, 1, 0));
        const Eigen::Quaternionf zRot(Eigen::AngleAxisf(ALXR::ToRadians(180.0f), Eigen::Vector3f(0,0,1)));
        leftHandBaseOrientation = yRot * zRot;
        return true;
    }

    bool IsHandTrackingEnabled() const override {
        for (const auto& handTracker : m_input.handTrackers) {
            if (handTracker.tracker != XR_NULL_HANDLE)
                return true;
        }
        return false;
    }

    bool InitializeFBPassthroughAPI()
    {
        if (m_instance == XR_NULL_HANDLE ||
            m_systemId == XR_NULL_SYSTEM_ID ||
            !IsExtEnabled(XR_FB_PASSTHROUGH_EXTENSION_NAME) ||
            (m_options && m_options->NoPassthrough))
        {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_PASSTHROUGH_EXTENSION_NAME));
            return false;
        }

        XrSystemPassthroughPropertiesFB passthroughSystemProperties{
            .type = XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB,
            .next = nullptr,
            .supportsPassthrough = XR_FALSE
        };
        XrSystemProperties systemProperties{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &passthroughSystemProperties
        };
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &systemProperties)) ||
            passthroughSystemProperties.supportsPassthrough == XR_FALSE) {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_FB_PASSTHROUGH_EXTENSION_NAME));
            return false;
        }
        Log::Write(Log::Level::Info, Fmt("%s enabled.", XR_FB_PASSTHROUGH_EXTENSION_NAME));

#define CAT(x,y) x ## y
#define INIT_PFN(ExtName)\
    CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xr"#ExtName, reinterpret_cast<PFN_xrVoidFunction*>(&CAT(m_pfn,ExtName))));

        INIT_PFN(CreatePassthroughFB);
        INIT_PFN(DestroyPassthroughFB);
        INIT_PFN(PassthroughStartFB);
        INIT_PFN(PassthroughPauseFB);
        INIT_PFN(CreatePassthroughLayerFB);
        INIT_PFN(DestroyPassthroughLayerFB);
        INIT_PFN(PassthroughLayerSetStyleFB);
        INIT_PFN(PassthroughLayerPauseFB);
        INIT_PFN(PassthroughLayerResumeFB);

#undef INIT_PFN
#undef CAT

        constexpr const XrPassthroughCreateInfoFB ptci {
            .type = XR_TYPE_PASSTHROUGH_CREATE_INFO_FB,
            .next = nullptr            
        };
        if (XR_FAILED(m_pfnCreatePassthroughFB(m_session, &ptci, &m_ptLayerData.passthrough))) {
            Log::Write(Log::Level::Error, "Failed to create passthrough object!");
            m_ptLayerData = {};
            return false;
        }

        const XrPassthroughLayerCreateInfoFB plci {
            .type = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB,
            .next = nullptr,
            .passthrough = m_ptLayerData.passthrough,
            .purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB
        };
        if (XR_FAILED(m_pfnCreatePassthroughLayerFB(m_session, &plci, &m_ptLayerData.reconPassthroughLayer))) {
            Log::Write(Log::Level::Error, "Failed to create passthrough layer!");
            m_ptLayerData = {};
            return false;
        }
        CHECK(m_ptLayerData.reconPassthroughLayer != XR_NULL_HANDLE);

        Log::Write(Log::Level::Info, "Passthrough API is initialized.");
        return true;
    }

    bool InitializeHTCPassthroughAPI()
    {
        if (m_instance == XR_NULL_HANDLE ||
            m_systemId == XR_NULL_SYSTEM_ID ||
            !IsExtEnabled(XR_HTC_PASSTHROUGH_EXTENSION_NAME) ||
            (m_options && m_options->NoPassthrough))
        {
            Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_HTC_PASSTHROUGH_EXTENSION_NAME));
            return false;
        }

        Log::Write(Log::Level::Info, Fmt("%s enabled.", XR_HTC_PASSTHROUGH_EXTENSION_NAME));

#define CAT(x,y) x ## y
#define INIT_PFN(ExtName)\
    CHECK_XRCMD(xrGetInstanceProcAddr(m_instance, "xr"#ExtName, reinterpret_cast<PFN_xrVoidFunction*>(&CAT(m_pfn,ExtName))));

        INIT_PFN(CreatePassthroughHTC);
        INIT_PFN(DestroyPassthroughHTC);

#undef INIT_PFN
#undef CAT
        CHECK(m_pfnCreatePassthroughHTC != nullptr);

        Log::Write(Log::Level::Info, "HTC Passthrough API is initialized.");
        return true;
    }

    void InitializePassthroughAPI() {
        if (InitializeFBPassthroughAPI())
            return;
        InitializeHTCPassthroughAPI();
    }

    bool HasOpaqueEnvBlendMode() const {
        return std::find(m_availableBlendModes.begin(), m_availableBlendModes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) != m_availableBlendModes.end();
    }

    std::optional<XrEnvironmentBlendMode> GetPassthroughEnvBlendMode() const {
        for (const auto nonOpaqueMode : NonOpaqueBlendModes) {
            if (std::find(m_availableBlendModes.begin(), m_availableBlendModes.end(), nonOpaqueMode) != m_availableBlendModes.end()) {
                return { nonOpaqueMode };
            }
        }
        return {};
    }

    inline bool IsPassthroughSupported() const {
        if (m_options && m_options->NoPassthrough)
            return false;
        if (m_ptLayerData.reconPassthroughLayer != XR_NULL_HANDLE)
            return true;
        if (m_pfnCreatePassthroughHTC != nullptr)
            return true;
#ifdef XR_USE_OXR_PICO
        if (m_pfnInvokeFunctionsPICO != nullptr)
            return true;
#endif
        return GetPassthroughEnvBlendMode().has_value();
    }

    std::atomic<ALXR::PassthroughMode> m_currentPTMode{ ALXR::PassthroughMode::None };
    inline bool IsPassthroughModeEnabled() const {
        return m_currentPTMode.load() != ALXR::PassthroughMode::None;
    }

#ifdef XR_USE_OXR_PICO
    bool SetPICOSeeThroughBackground(const bool enable) {
        if (m_session == XR_NULL_HANDLE ||
            m_pfnInvokeFunctionsPICO == nullptr ||
            (m_options && m_options->NoPassthrough))
            return false;

        const auto graphicsPluginPtr = m_graphicsPlugin;
        if (graphicsPluginPtr == nullptr)
            return false;

        // When enabled, clear color must be rgba == 0x00000000 (same as additive blend mode).
        const auto newBlendMode = enable ?
            XR_ENVIRONMENT_BLEND_MODE_ADDITIVE : m_environmentBlendMode.load();
        graphicsPluginPtr->SetEnvironmentBlendMode(newBlendMode);

        XrBool32 enableSeethroughBackground = enable ? XR_TRUE : XR_FALSE;
        return m_pfnInvokeFunctionsPICO
        (
            m_session,
            XR_SET_SEETHROUGH_BACKGROUND,
            &enableSeethroughBackground,
            sizeof(enableSeethroughBackground),
            nullptr, 0
        ) == XR_SUCCESS;
    }
#endif

    bool StartPassthroughMode() {
        if (!IsPassthroughSupported())
            return false;

        if (m_ptLayerData.reconPassthroughLayer != XR_NULL_HANDLE) {
            if (XR_FAILED(m_pfnPassthroughStartFB(m_ptLayerData.passthrough))) {
                Log::Write(Log::Level::Warning, "[XR_FB_passthrough] Failed to start passthrough.");
                return false;
            }
            if (XR_FAILED(m_pfnPassthroughLayerResumeFB(m_ptLayerData.reconPassthroughLayer))) {
                Log::Write(Log::Level::Warning, "[XR_FB_passthrough] Failed to resumed passthrough layer.");
                return false;
            }
            Log::Write(Log::Level::Info, "[XR_FB_passthrough] Passthrough (Layer) is started/resumed.");

            constexpr const XrPassthroughStyleFB style{
                .type = XR_TYPE_PASSTHROUGH_STYLE_FB,
                .next = nullptr,
                .textureOpacityFactor = 0.5f,
                .edgeColor = { 0.0f, 0.0f, 0.0f, 0.0f },
            };
            const bool result = XR_SUCCEEDED(m_pfnPassthroughLayerSetStyleFB(m_ptLayerData.reconPassthroughLayer, &style));
            Log::Write(Log::Level::Verbose, Fmt("[XR_FB_passthrough] Passthrough layer style %s set.", result ? "successfully" : "failed to be"));
            return result;
        }

        if (m_pfnCreatePassthroughHTC && m_ptLayerData.passthroughHTC == XR_NULL_HANDLE) {
            constexpr const XrPassthroughCreateInfoHTC createInfo {
                .type = XR_TYPE_PASSTHROUGH_CREATE_INFO_HTC,
                .next = nullptr,
                .form = XR_PASSTHROUGH_FORM_PLANAR_HTC,
            };
            const bool result =  XR_SUCCEEDED(m_pfnCreatePassthroughHTC(m_session, &createInfo, &m_ptLayerData.passthroughHTC));
            Log::Write(Log::Level::Info, Fmt("[XR_HTC_passthrough] Passthrough %s started/resumed.", result ? "successfully" : "failed to be"));
            return result;
        }

#ifdef XR_USE_OXR_PICO
        if (IsPrePicoPUI<5,8>() && m_pfnInvokeFunctionsPICO) {
            const bool result = SetPICOSeeThroughBackground(true);
            Log::Write(Log::Level::Info, Fmt("[XR_PICO_boundary(_ext)] Passthrough (Layer) %s started/resumed.", result ? "successfully" : "failed to be"));
            return result;
        }
#endif

        if (m_environmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
            const auto ptEnvBlendMode = GetPassthroughEnvBlendMode();
            if (!ptEnvBlendMode.has_value())
                return false;
            const auto graphicsPluginPtr = m_graphicsPlugin;
            if (graphicsPluginPtr == nullptr)
                return false;
            graphicsPluginPtr->SetEnvironmentBlendMode(ptEnvBlendMode.value());
            m_environmentBlendMode.store(ptEnvBlendMode.value());
            Log::Write(Log::Level::Info, Fmt("Enviroment blend mode set to %d for passthrough modes.", ptEnvBlendMode.value()));
            return true;
        }

        return false;
    }

    bool StopPassthroughMode() {
        if (!IsPassthroughModeEnabled())
            return false;
        assert(IsPassthroughSupported());
        m_currentPTMode.store(ALXR::PassthroughMode::None);
    /////////////////////////////////////////////////
        Log::Write(Log::Level::Info, "Passthrough (Layer) is stopped/paused.");
        if (m_ptLayerData.reconPassthroughLayer != XR_NULL_HANDLE) {
            if (XR_FAILED(m_pfnPassthroughLayerPauseFB(m_ptLayerData.reconPassthroughLayer))) {
                Log::Write(Log::Level::Warning, "[XR_FB_passthrough] Failed to pause passthrough layer.");
            }
            if (XR_FAILED(m_pfnPassthroughPauseFB(m_ptLayerData.passthrough))) {
                Log::Write(Log::Level::Warning, "[XR_FB_passthrough] Failed to pause/stop passthrough.");
                return false;
            }
            return true;
        }

        if (m_ptLayerData.passthroughHTC != XR_NULL_HANDLE && m_pfnDestroyPassthroughHTC) {
            const bool result = XR_SUCCEEDED(m_pfnDestroyPassthroughHTC(m_ptLayerData.passthroughHTC));
            m_ptLayerData.passthroughHTC = XR_NULL_HANDLE;
            if (!result)
                Log::Write(Log::Level::Warning, "[XR_HTC_passthrough] Failed to pause/stop passthrough.");
            return result;
        }

#ifdef XR_USE_OXR_PICO
        if (IsPrePicoPUI<5, 8>() && m_pfnInvokeFunctionsPICO) {
            const bool result = SetPICOSeeThroughBackground(false);
            Log::Write(Log::Level::Info, Fmt("[XR_PICO_boundary(_ext)] Passthrough (Layer) %s paused/stopped.", result ? "successfully" : "failed to be"));
            return result;
        }
#endif

        if (m_environmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE && HasOpaqueEnvBlendMode()) {
            const auto graphicsPluginPtr = m_graphicsPlugin;
            if (graphicsPluginPtr == nullptr)
                return false;
            graphicsPluginPtr->SetEnvironmentBlendMode(XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
            m_environmentBlendMode.store(XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
            Log::Write(Log::Level::Info, "Enviroment blend mode set to OPAQUE for normal renader mode (no passthrough).");
            return true;
        }

        return false;
    }

    void TogglePassthroughMode(const ALXR::PassthroughMode newMode) {
        assert(IsPassthroughSupported());
        const auto lastMode = m_currentPTMode.load();
        if (newMode == lastMode) {
            StopPassthroughMode();
            return;
        }
        if (lastMode == ALXR::PassthroughMode::None) {
            StartPassthroughMode();
        }
        /////////////////////////////////////////////////
        m_currentPTMode.store(newMode);
        Log::Write(Log::Level::Info, Fmt("Passthrough mode changed from %u to %u", lastMode, newMode));
    }

    void SetPerformanceLevels() {
        if (!IsSessionRunning())
            return;

        if (IsExtEnabled(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME)) {
            PFN_xrPerfSettingsSetPerformanceLevelEXT setPerfLevel = nullptr;
            if (XR_FAILED(xrGetInstanceProcAddr
            (
                m_instance,
                "xrPerfSettingsSetPerformanceLevelEXT",
                reinterpret_cast<PFN_xrVoidFunction*>(&setPerfLevel)
            ))) {
                Log::Write(Log::Level::Warning, "xrGetInstanceProcAddr failed to get address for xrPerfSettingsSetPerformanceLevelEXT.");
                setPerfLevel = nullptr;
            }
            if (setPerfLevel) {
                if (XR_FAILED(setPerfLevel(m_session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT)))
                    Log::Write(Log::Level::Warning, "Failed to set CPU performance level");
                if (XR_FAILED(setPerfLevel(m_session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_BOOST_EXT)))
                    Log::Write(Log::Level::Warning, "Failed to set GPU performance level");
            }
        }
    }

#ifdef XR_USE_PLATFORM_ANDROID
    inline bool SetAndroidAppThread(const pid_t threadId, const AndroidThreadType threadType) {
        if (!IsSessionRunning() ||
            !IsExtEnabled(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME))
            return false;
        
        PFN_xrSetAndroidApplicationThreadKHR setAndroidAppThreadFn = nullptr;
        if (XR_FAILED(xrGetInstanceProcAddr
        (
            m_instance,
            "xrSetAndroidApplicationThreadKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&setAndroidAppThreadFn)
        ))) {
            Log::Write(Log::Level::Warning, "xrGetInstanceProcAddr failed to get address for xrSetAndroidApplicationThreadKHR.");
            setAndroidAppThreadFn = nullptr;
            return false;
        }
        Log::Write(Log::Level::Info, Fmt("Setting android app thread, type: %lu, thread-id: %d", threadType, threadId));
        const auto result = setAndroidAppThreadFn(m_session, static_cast<XrAndroidThreadTypeKHR>(threadType), threadId);
        if (XR_FAILED(result)) {
            Log::Write(Log::Level::Info, Fmt("Failed setting android app thread, type: %lu, thread-id: %d, result-id: %lu", threadType, threadId, result));
        }
        return XR_SUCCEEDED(result);
    }

    virtual inline bool SetAndroidAppThread(const AndroidThreadType threadType) override {
        return SetAndroidAppThread(gettid(), threadType);
    }
#endif

    void CreateVisualizedSpaces() {
        CHECK(m_session != XR_NULL_HANDLE);
#ifdef ALXR_ENGINE_ENABLE_VIZ_SPACES
        constexpr const std::string_view visualizedSpaces[] = { "ViewFront", "Local", "Stage", "StageLeft", "StageRight", "StageLeftRotated", "StageRightRotated" };

        for (const auto& visualizedSpace : visualizedSpaces) {
            XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = GetXrReferenceSpaceCreateInfo(visualizedSpace);
            XrSpace space;
            XrResult res = xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &space);
            if (XR_SUCCEEDED(res)) {
                m_visualizedSpaces.push_back(space);
                Log::Write(Log::Level::Info, Fmt("visualized-space %s added", visualizedSpace.data()));
            } else {
                Log::Write(Log::Level::Warning,
                           Fmt("Failed to create reference space %s with error %d", visualizedSpace.data(), res));
            }
        }
#endif
    }

    void InitializeSession() override {
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId != XR_NULL_SYSTEM_ID);
        CHECK(m_session == XR_NULL_HANDLE);
        {
            Log::Write(Log::Level::Verbose, Fmt("Creating session..."));

            const XrSessionCreateInfo createInfo{
                .type = XR_TYPE_SESSION_CREATE_INFO,
                .next = m_graphicsPlugin->GetGraphicsBinding(),
                .createFlags = 0,
                .systemId = m_systemId
            };
            CHECK_XRCMD(xrCreateSession(m_instance, &createInfo, &m_session));
            CHECK(m_session != XR_NULL_HANDLE);
        }
        
        InitializeExtensions();
        LogReferenceSpaces();
        InitializeActions();
        CreateVisualizedSpaces();

        {
            XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = GetAppReferenceSpaceCreateInfo();
            CHECK_XRCMD(xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &m_appSpace));
            Log::Write(Log::Level::Verbose, Fmt("Selected app reference space: %s", to_string(referenceSpaceCreateInfo.referenceSpaceType)));
            m_streamConfig.trackingSpaceType = ToTrackingSpace(referenceSpaceCreateInfo.referenceSpaceType);

            referenceSpaceCreateInfo = GetXrReferenceSpaceCreateInfo("Stage");
            if (XR_FAILED(xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &m_boundingStageSpace)))
                m_boundingStageSpace = XR_NULL_HANDLE;

            referenceSpaceCreateInfo = GetXrReferenceSpaceCreateInfo("View");
            CHECK_XRCMD(xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &m_viewSpace));
        }

        if (m_options && IsPassthroughSupported()) {
            const auto newPtMode = static_cast<ALXR::PassthroughMode>(m_options->PassthroughMode);
            TogglePassthroughMode(newPtMode);
        }
    }

    void ClearSwapchains()
    {
        m_swapchainImages.clear();
        if (const auto graphicsPlugin = m_graphicsPlugin) {
            graphicsPlugin->ClearSwapchainImageStructs();
        }
        for (const auto& swapchain : m_swapchains)
            xrDestroySwapchain(swapchain.handle);
        m_swapchains.clear();
        m_configViews.clear();
    }

    bool GetHiddenAreaMesh(size_t viewIdx, IOpenXrProgram::HiddenAreaMesh& mesh) const override {
        if (!IsExtEnabled(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME))
            return false;

        PFN_xrGetVisibilityMaskKHR xrGetVisibilityMaskKHR = nullptr;
        if (XR_FAILED(xrGetInstanceProcAddr(m_instance, "xrGetVisibilityMaskKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVisibilityMaskKHR))) ||
            xrGetVisibilityMaskKHR == nullptr) {
            Log::Write(Log::Level::Warning, "xrGetInstanceProcAddr failed to get address for xrGetVisibilityMaskKHR.");
            return false;
        }
        
        constexpr const XrVisibilityMaskTypeKHR visibilityMaskType = XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR;

        XrVisibilityMaskKHR visibilityMask = {
            .type = XR_TYPE_VISIBILITY_MASK_KHR,
            .next = nullptr,
            .vertexCapacityInput = 0,
            .vertexCountOutput = 0,
            .vertices = nullptr,
            .indexCapacityInput = 0,
            .indexCountOutput = 0,
            .indices = nullptr
        };
        auto result = xrGetVisibilityMaskKHR(
            m_session, m_viewConfigType, (uint32_t)viewIdx,
            visibilityMaskType, &visibilityMask
        );
        if (XR_FAILED(result)) {
            Log::Write(Log::Level::Error, Fmt("xrGetVisibilityMaskKHR failed with error %d", result));
            return false;
        }

        if (visibilityMask.indexCountOutput == 0 || visibilityMask.vertexCountOutput == 0) {
            Log::Write(Log::Level::Warning, "Runtime has no valid visibility mask.");
            return false;
        }

        mesh.indices.resize(visibilityMask.indexCountOutput);
        visibilityMask.indexCapacityInput = visibilityMask.indexCountOutput;
        visibilityMask.indices = mesh.indices.data();

        mesh.vertices.resize(visibilityMask.vertexCountOutput, { 0,0 });
        visibilityMask.vertexCapacityInput = visibilityMask.vertexCountOutput;
        visibilityMask.vertices = mesh.vertices.data();

        result = xrGetVisibilityMaskKHR(
            m_session, m_viewConfigType, (uint32_t)viewIdx,
            visibilityMaskType, &visibilityMask
        );
        if (XR_FAILED(result)) {
            Log::Write(Log::Level::Error, Fmt("xrGetVisibilityMaskKHR failed with error %d", result));
            return false;
        }

#ifdef XR_USE_PLATFORM_WIN32
        // Meta-link (not standalone) runtime outputs visiblity mask upside down,
        if (IsRuntime(OxrRuntimeType::Oculus) && mesh.vertices.size() > 0) {
            const auto [minY, maxY] =
                std::minmax_element(mesh.vertices.begin(), mesh.vertices.end(), [](const auto& v1, const auto& v2) {
                    return v1.y < v2.y;
                });
            const float flipAxisY = minY->y + maxY->y;
            for (auto& vert : mesh.vertices) {
                vert.y = flipAxisY - vert.y;
            }
        }
#endif

#ifdef ALXR_DEBUG_VISIBILITY_MASK
        for (std::size_t idx = 0; idx < vertices.size(); ++idx) {
            Log::Write(Log::Level::Verbose,
                Fmt("visibility mask vertex-%d: (%f, %f)", idx, vertices[idx].x, vertices[idx].y));
        }
        for (std::size_t idx = 0; idx < indices.size(); ++idx) {
            Log::Write(Log::Level::Verbose,
                Fmt("visibility mask index-%d: %u", idx, indices[idx]));
        }
#endif
        return true;
    }

    bool UpdateHiddenAreaMeshes() {
        if (!IsExtEnabled(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME))
            return true;
        assert(m_graphicsPlugin != nullptr);
        for (std::uint32_t viewIdx = 0; viewIdx < m_views.size(); ++viewIdx) {
            IOpenXrProgram::HiddenAreaMesh ham = {};
            if (!GetHiddenAreaMesh(viewIdx, ham) || !ham.IsValid())
                break;
            const XrVisibilityMaskKHR visibilityMask = {
                .type = XR_TYPE_VISIBILITY_MASK_KHR,
                .next = nullptr,
                .vertexCapacityInput = (uint32_t)ham.vertices.size(),
                .vertexCountOutput = (uint32_t)ham.vertices.size(),
                .vertices = ham.vertices.data(),
                .indexCapacityInput = (uint32_t)ham.indices.size(),
                .indexCountOutput = (uint32_t)ham.indices.size(),
                .indices = ham.indices.data()
            };
            if (!m_graphicsPlugin->SetVisibilityMask(viewIdx, visibilityMask)) {
                Log::Write(Log::Level::Error, Fmt("SetVisibilityMask failed"));
                return false;
            }
        }
        // TODO: Refactor OpenXR event handling and input thread
        //       and send new view config message (with updated HAMs) when
        //       visibility mask change events occur.
        return true;
    }

    void CreateSwapchains(const std::uint32_t eyeWidth /*= 0*/, const std::uint32_t eyeHeight /*= 0*/) override {
        CHECK(m_session != XR_NULL_HANDLE);

        if (m_swapchains.size() > 0)
        {
            CHECK(m_configViews.size() > 0 && m_swapchainImages.size() > 0);
            if (eyeWidth == 0 || eyeHeight == 0)
                return;
            const bool isSameSize = std::all_of(m_configViews.begin(), m_configViews.end(), [&](const auto& vp)
            {
                const auto eW = std::min(eyeWidth,  vp.maxImageRectWidth);
                const auto eH = std::min(eyeHeight, vp.maxImageRectHeight);
                return eW == vp.recommendedImageRectWidth && eH == vp.recommendedImageRectHeight;
            });
            if (isSameSize)
                return;
            Log::Write(Log::Level::Info, "Clearing current swapchains...");
            ClearSwapchains();
            Log::Write(Log::Level::Info, "Creating new swapchains...");
        }
        CHECK(m_swapchainImages.empty());
        CHECK(m_swapchains.empty());
        //CHECK(m_configViews.empty());

        // Read graphics properties for preferred swapchain length and logging.
        XrSystemProperties systemProperties{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = nullptr
        };
        CHECK_XRCMD(xrGetSystemProperties(m_instance, m_systemId, &systemProperties));

        // Log system properties.
        Log::Write(Log::Level::Info,
                   Fmt("System Properties: Name=%s VendorId=%d", systemProperties.systemName, systemProperties.vendorId));
        Log::Write(Log::Level::Info, Fmt("System Graphics Properties: MaxWidth=%d MaxHeight=%d MaxLayers=%d",
                                         systemProperties.graphicsProperties.maxSwapchainImageWidth,
                                         systemProperties.graphicsProperties.maxSwapchainImageHeight,
                                         systemProperties.graphicsProperties.maxLayerCount));
        Log::Write(Log::Level::Info, Fmt("System Tracking Properties: OrientationTracking=%s PositionTracking=%s",
                                         systemProperties.trackingProperties.orientationTracking == XR_TRUE ? "True" : "False",
                                         systemProperties.trackingProperties.positionTracking == XR_TRUE ? "True" : "False"));

        // Note: No other view configurations exist at the time this code was written. If this
        // condition is not met, the project will need to be audited to see how support should be
        // added.
        CHECK_MSG(m_viewConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, "Unsupported view configuration type");

        // Query and cache view configuration views.
        uint32_t viewCount = 0;
        CHECK_XRCMD(xrEnumerateViewConfigurationViews(m_instance, m_systemId, m_viewConfigType, 0, &viewCount, nullptr));
        CHECK(viewCount >= 2);
        m_configViews.resize(viewCount, {
            .type = XR_TYPE_VIEW_CONFIGURATION_VIEW,
            .next = nullptr
        });
        CHECK_XRCMD(xrEnumerateViewConfigurationViews(m_instance, m_systemId, m_viewConfigType, viewCount, &viewCount,
                                                      m_configViews.data()));

        // override recommended eye resolution
        if (eyeWidth != 0 && eyeHeight != 0) {
            for (auto& configView : m_configViews) {
                configView.recommendedImageRectWidth  = std::min(eyeWidth, configView.maxImageRectWidth);
                configView.recommendedImageRectHeight = std::min(eyeHeight, configView.maxImageRectHeight);
            }
        }

        // Create and cache view buffer for xrLocateViews later.
        m_views.resize(viewCount, ALXR::IdentityView);
        if (viewCount < 2)
            return;

        if (IsHeadlessSession())
            return;

        // Create the swapchain and get the images.
        // 
        // Select a swapchain format.
        uint32_t swapchainFormatCount = 0;
        CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session, 0, &swapchainFormatCount, nullptr));
        std::vector<int64_t> swapchainFormats(swapchainFormatCount);
        CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session, (uint32_t)swapchainFormats.size(), &swapchainFormatCount,
                                                swapchainFormats.data()));
        CHECK(swapchainFormatCount == swapchainFormats.size());
        m_colorSwapchainFormat = m_graphicsPlugin->SelectColorSwapchainFormat(swapchainFormats);
        CHECK_MSG(m_colorSwapchainFormat > 0, "No runtime swapchain format supported for color swapchain");

        // Print swapchain formats and the selected one.
        {
            std::string swapchainFormatsString;
            for (int64_t format : swapchainFormats) {
                const bool selected = format == m_colorSwapchainFormat;
                swapchainFormatsString += " ";
                if (selected) {
                    swapchainFormatsString += "[";
                }
                swapchainFormatsString += std::to_string(format);
                if (selected) {
                    swapchainFormatsString += "]";
                }
            }
            Log::Write(Log::Level::Verbose, Fmt("Swapchain Formats: %s", swapchainFormatsString.c_str()));
        }

        if (m_isMultiViewEnabled)
        {
            const XrViewConfigurationView& vp = [&]() {
                assert(m_configViews.size() > 1);
                const auto vpItr = std::max_element(m_configViews.begin(), m_configViews.end(), [](const auto& vp1, const auto& vp2) {
                    const size_t vp1Size = vp1.recommendedImageRectWidth * vp1.recommendedImageRectHeight;
                    const size_t vp2Size = vp2.recommendedImageRectWidth * vp2.recommendedImageRectHeight;
                    return vp1Size < vp2Size;
                });
                if (vpItr == m_configViews.end())
                    return m_configViews[0];
                return *vpItr;
            }();
            
            Log::Write(Log::Level::Info, Fmt
            (
                "Creating swapchain with texture array size=%" PRIu32
                ", dimensions Width=%" PRIu32 " Height=%" PRIu32 " SampleCount=%" PRIu32, viewCount,
                vp.recommendedImageRectWidth, vp.recommendedImageRectHeight, vp.recommendedSwapchainSampleCount
            ));

            // Create the swapchain.
            const XrSwapchainCreateInfo swapchainCreateInfo{
                .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                .next = nullptr,
                .createFlags = 0,
                .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                .format = m_colorSwapchainFormat,
                .sampleCount = m_graphicsPlugin->GetSupportedSwapchainSampleCount(vp),
                .width = vp.recommendedImageRectWidth,
                .height = vp.recommendedImageRectHeight,
                .faceCount = 1,
                .arraySize = viewCount,
                .mipCount = 1,
            };
            Swapchain swapchain{
                .handle = XR_NULL_HANDLE,
                .width = static_cast<std::int32_t>(swapchainCreateInfo.width),
                .height = static_cast<std::int32_t>(swapchainCreateInfo.height)
            };
            CHECK_XRCMD(xrCreateSwapchain(m_session, &swapchainCreateInfo, &swapchain.handle));
            CHECK(swapchain.handle != XR_NULL_HANDLE);

            m_swapchains.push_back(swapchain);

            uint32_t imageCount = 0;
            CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr));
            // XXX This should really just return XrSwapchainImageBaseHeader*
            std::vector<XrSwapchainImageBaseHeader*> swapchainImages =
                m_graphicsPlugin->AllocateSwapchainImageStructs(imageCount, swapchainCreateInfo);
            CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount, swapchainImages[0]));

            m_swapchainImages.insert(std::make_pair(swapchain.handle, std::move(swapchainImages)));
        }
        else
        {
            // Create a swapchain for each view.
            for (uint32_t i = 0; i < viewCount; i++) {
                const XrViewConfigurationView& vp = m_configViews[i];
                Log::Write(Log::Level::Info,
                    Fmt("Creating swapchain for view %d with dimensions Width=%d Height=%d SampleCount=%d", i,
                        vp.recommendedImageRectWidth, vp.recommendedImageRectHeight, vp.recommendedSwapchainSampleCount));

                // Create the swapchain.
                const XrSwapchainCreateInfo swapchainCreateInfo{
                    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                    .next = nullptr,
                    .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                    .format = m_colorSwapchainFormat,
                    .sampleCount = m_graphicsPlugin->GetSupportedSwapchainSampleCount(vp),
                    .width = vp.recommendedImageRectWidth,
                    .height = vp.recommendedImageRectHeight,
                    .faceCount = 1,
                    .arraySize = 1,
                    .mipCount = 1,
                };
                Swapchain swapchain{
                    .handle = XR_NULL_HANDLE,
                    .width = static_cast<std::int32_t>(swapchainCreateInfo.width),
                    .height = static_cast<std::int32_t>(swapchainCreateInfo.height)
                };
                CHECK_XRCMD(xrCreateSwapchain(m_session, &swapchainCreateInfo, &swapchain.handle));
                CHECK(swapchain.handle != XR_NULL_HANDLE);

                m_swapchains.push_back(swapchain);

                uint32_t imageCount = 0;
                CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr));
                // XXX This should really just return XrSwapchainImageBaseHeader*
                std::vector<XrSwapchainImageBaseHeader*> swapchainImages =
                    m_graphicsPlugin->AllocateSwapchainImageStructs(imageCount, swapchainCreateInfo);
                CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount, swapchainImages[0]));

                m_swapchainImages.insert(std::make_pair(swapchain.handle, std::move(swapchainImages)));
            }
        }

        UpdateHiddenAreaMeshes();
    }

    // Return event if one is available, otherwise return null.
    const XrEventDataBaseHeader* TryReadNextEvent() {
        // It is sufficient to clear the just the XrEventDataBuffer header to
        // XR_TYPE_EVENT_DATA_BUFFER
        XrEventDataBaseHeader* baseHeader = reinterpret_cast<XrEventDataBaseHeader*>(&m_eventDataBuffer);
        *baseHeader = {.type=XR_TYPE_EVENT_DATA_BUFFER, .next=nullptr};
        const XrResult xr = xrPollEvent(m_instance, &m_eventDataBuffer);
        if (xr == XR_SUCCESS) {
            if (baseHeader->type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
                const XrEventDataEventsLost* const eventsLost = reinterpret_cast<const XrEventDataEventsLost*>(baseHeader);
                Log::Write(Log::Level::Warning, Fmt("%d events lost", eventsLost->lostEventCount));
            }
            return baseHeader;
        }
        if (xr == XR_EVENT_UNAVAILABLE) {
            return nullptr;
        }
        THROW_XR(xr, "xrPollEvent");
    }

    void PollEvents(bool* exitRenderLoop, bool* requestRestart) override {
        assert(exitRenderLoop != nullptr && requestRestart != nullptr);
        *exitRenderLoop = *requestRestart = false;

        PollStreamConfigEvents();
        bool visibilityMaskChanged = false;

        // Process all pending messages.
        while (const XrEventDataBaseHeader* event = TryReadNextEvent()) {
            switch (event->type) {
                case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
                    const auto& refreshRateChangedEvent = *reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB*>(event);
                    Log::Write(Log::Level::Info, Fmt("display refresh rate has changed from %f Hz to %f Hz", refreshRateChangedEvent.fromDisplayRefreshRate, refreshRateChangedEvent.toDisplayRefreshRate));
                    m_streamConfig.renderConfig.refreshRate = refreshRateChangedEvent.toDisplayRefreshRate;
                    break;
                }
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                    const auto& instanceLossPending = *reinterpret_cast<const XrEventDataInstanceLossPending*>(event);
                    Log::Write(Log::Level::Warning, Fmt("XrEventDataInstanceLossPending by %lld", instanceLossPending.lossTime));
                    *exitRenderLoop = true;
                    *requestRestart = true;
                    return;
                }
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                    const auto& sessionStateChangedEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(event);
                    HandleSessionStateChangedEvent(sessionStateChangedEvent, exitRenderLoop, requestRestart);
                    break;
                }
                case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
                    m_interactionManager->SetActiveFromCurrentProfile();
                    m_interactionManager->LogActions();
                    break;
                }
                case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
                    const auto& perfSettingsEvent = *reinterpret_cast<const XrEventDataPerfSettingsEXT*>(event);
                    Log::Write(Log::Level::Info,
                        Fmt("PerfSettingsChanged: type %d subdomain %d : level %d -> level %d",
                            perfSettingsEvent.type,
                            perfSettingsEvent.subDomain,
                            perfSettingsEvent.fromLevel,
                            perfSettingsEvent.toLevel));
                } break;
                case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
                    const auto& spaceChangedEvent = *reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(event);
                    Log::Write(Log::Level::Verbose, Fmt("reference space: %d changing", spaceChangedEvent.referenceSpaceType));
                    const auto appRefSpace = ToXrReferenceSpaceType(m_streamConfig.trackingSpaceType);
                    if (spaceChangedEvent.referenceSpaceType == appRefSpace)
                        enqueueGuardianChanged(spaceChangedEvent.changeTime);
                }  break;
                case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR: {
                    const auto& visibilityMaskChangedEvent = *reinterpret_cast<const XrEventDataVisibilityMaskChangedKHR*>(event);
                    Log::Write(Log::Level::Verbose,
                               Fmt("visibility mask changed for view %d", visibilityMaskChangedEvent.viewIndex));
                    visibilityMaskChanged = true;
                    break;
                }
                default: {
                    Log::Write(Log::Level::Verbose, Fmt("Ignoring event type %d", event->type));
                    break;
                }
            }
        }
        if (visibilityMaskChanged) {
            UpdateHiddenAreaMeshes();
        }
    }

    void HandleSessionStateChangedEvent(const XrEventDataSessionStateChanged& stateChangedEvent, bool* exitRenderLoop,
                                        bool* requestRestart) {
        const XrSessionState oldState = m_sessionState;
        m_sessionState = stateChangedEvent.state;

        Log::Write(Log::Level::Info, Fmt("XrEventDataSessionStateChanged: state %s->%s session=%lld time=%lld", to_string(oldState),
                                         to_string(m_sessionState), stateChangedEvent.session, stateChangedEvent.time));

        if ((stateChangedEvent.session != XR_NULL_HANDLE) && (stateChangedEvent.session != m_session)) {
            Log::Write(Log::Level::Error, "XrEventDataSessionStateChanged for unknown session");
            return;
        }

        switch (m_sessionState) {
            case XR_SESSION_STATE_SYNCHRONIZED: {
                m_delayOnGuardianChanged = true;
                break;
            }
            case XR_SESSION_STATE_READY: {
                CHECK(m_session != XR_NULL_HANDLE);
                const XrSessionBeginInfo sessionBeginInfo{
                    .type = XR_TYPE_SESSION_BEGIN_INFO,
                    .next = nullptr,
                    .primaryViewConfigurationType = m_viewConfigType
                };
                XrResult result;
                CHECK_XRCMD(result = xrBeginSession(m_session, &sessionBeginInfo));
                m_sessionRunning = (result == XR_SUCCESS);
                SetPerformanceLevels();
                SetAndroidAppThread(AndroidThreadType::AppMain);
                SetAndroidAppThread(AndroidThreadType::RendererMain);
                break;
            }
            case XR_SESSION_STATE_STOPPING: {
                CHECK(m_session != XR_NULL_HANDLE);
                StopPassthroughMode();
                CHECK_XRCMD(xrEndSession(m_session))
                m_sessionRunning = false;
                break;
            }
            case XR_SESSION_STATE_EXITING: {
                *exitRenderLoop = true;
                // Do not attempt to restart because user closed this session.
                *requestRestart = false;
                break;
            }
            case XR_SESSION_STATE_LOSS_PENDING: {
                *exitRenderLoop = true;
                // Poll for a new instance.
                *requestRestart = true;
                break;
            }
            default:
                break;
        }
    }

    bool IsSessionRunning() const override { return m_sessionRunning; }
    bool IsSessionFocused() const override { return m_sessionState == XR_SESSION_STATE_FOCUSED; }

    bool IsHeadlessSession() const override {
        if (m_options == nullptr)
            return false;
        return m_options->SimulateHeadless || (m_options->HeadlessSession && IsExtEnabled(XR_MND_HEADLESS_EXTENSION_NAME));
    }

    template < typename ControllerInfoArray >
    void PollHandTrackers(const XrTime time, ControllerInfoArray& controllerInfo)
    {
        if (m_pfnLocateHandJointsEXT == nullptr || time == 0)
            return;

        const bool isHandOnControllerPose = //IsRuntime(OxrRuntimeType::HTCWave) ||
                                            IsRuntime(OxrRuntimeType::SteamVR) ||
                                            IsRuntime(OxrRuntimeType::WMR) ||
                                            IsRuntime(OxrRuntimeType::MagicLeap);
        std::array<Eigen::Affine3f, XR_HAND_JOINT_COUNT_EXT> oculusOrientedJointPoses;
        for (const auto hand : { Side::LEFT,Side::RIGHT })
        {
            auto& controller = controllerInfo[hand];
            // TODO: v17/18 server does not allow for both controller & hand tracking data, this needs changing,
            //       we don't want to override a controller device pose with potentially an emulated pose for
            //       runtimes such as WMR & SteamVR.
            if (isHandOnControllerPose && controller.enabled)
                continue;

            auto& handTracker = m_input.handTrackers[hand];
            if (handTracker.tracker == XR_NULL_HANDLE)
                continue;

            //XrHandJointVelocitiesEXT velocities {
            //    .type = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
            //    .next = nullptr,
            //    .jointCount = XR_HAND_JOINT_COUNT_EXT,
            //    .jointVelocities = handTracker.jointVelocities.data(),
            //};
            XrHandJointLocationsEXT locations {
                .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
                .next = nullptr, //&velocities,
                .isActive = XR_FALSE,
                .jointCount = XR_HAND_JOINT_COUNT_EXT,
                .jointLocations = handTracker.jointLocations.data(),
            };
            const XrHandJointsLocateInfoEXT locateInfo{
                .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
                .next = nullptr,
                .baseSpace = m_appSpace,
                .time = time
            };
            if (XR_FAILED(m_pfnLocateHandJointsEXT(handTracker.tracker, &locateInfo, &locations)) ||
                locations.isActive == XR_FALSE)
                continue;

            const auto& jointLocations = handTracker.jointLocations;
            const auto& handBaseOrientation = handTracker.baseOrientation;
            for (size_t jointIdx = 0; jointIdx < XR_HAND_JOINT_COUNT_EXT; ++jointIdx)
            {
                const auto& jointLoc = jointLocations[jointIdx];
                Eigen::Affine3f& jointMatFixed = oculusOrientedJointPoses[jointIdx];
                jointMatFixed.setIdentity();
                if (!Math::Pose::IsPoseValid(jointLoc)) {
                    continue;
                }
                jointMatFixed = ALXR::ToAffine3f(jointLoc.pose) * handBaseOrientation;
            }
            
            for (size_t boneIndex = 0; boneIndex < ALVR_HAND::alvrHandBone_MaxSkinnable; ++boneIndex)
            {
                auto& boneRot = controller.boneRotations[boneIndex];
                auto& bonePos = controller.bonePositionsBase[boneIndex];
                boneRot = { 0,0,0,1 };
                bonePos = { 0,0,0 };
                
                const auto xrJoint = ToXRHandJointType(static_cast<const ALVR_HAND>(boneIndex));
                if (xrJoint == XR_HAND_JOINT_MAX_ENUM_EXT)
                    continue;

                const auto xrJointParent = GetJointParent(xrJoint);
                const Eigen::Affine3f& jointParentWorld = oculusOrientedJointPoses[xrJointParent];
                const Eigen::Affine3f& JointWorld       = oculusOrientedJointPoses[xrJoint];

                const Eigen::Affine3f jointLocal = jointParentWorld.inverse() * JointWorld;

                const Eigen::Quaternionf localizedRot(jointLocal.rotation());
                boneRot = ToALXRQuaternionf(localizedRot);
                bonePos = ToALXRVector3f(jointLocal.translation());
            }

            controller.enabled = true;
            controller.isHand = true;

            const Eigen::Affine3f& palmMatP = oculusOrientedJointPoses[XR_HAND_JOINT_PALM_EXT];
            controller.boneRootPose    = ToALXRPosef(palmMatP);
            controller.linearVelocity  = { 0,0,0 };
            controller.angularVelocity = { 0,0,0 };
        }
    }

    void PollHandTracking(const XrTime& time, ALXRHandTracking& handTrackingData) {

        static_assert(sizeof(ALXRHandJointLocation) == sizeof(XrHandJointLocationEXT));
        static_assert(sizeof(ALXRHandJointVelocity) == sizeof(XrHandJointVelocityEXT));
        static_assert(MaxHandJointCount == XR_HAND_JOINT_COUNT_EXT);

        for (const auto hand : { Side::LEFT,Side::RIGHT })
        {
            auto& handData = handTrackingData.hands[hand];
            handData.isActive = false;
            if (time == 0 || m_pfnLocateHandJointsEXT == nullptr)
                continue;

            auto& handTracker = m_input.handTrackers[hand];
            if (handTracker.tracker == XR_NULL_HANDLE)
                continue;

            XrHandJointVelocitiesEXT velocities{
                .type = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
                .next = nullptr,
                .jointCount = XR_HAND_JOINT_COUNT_EXT,
                .jointVelocities = reinterpret_cast<XrHandJointVelocityEXT*>(&handData.jointVelocities[0]),
            };
            XrHandJointLocationsEXT locations{
                .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
                .next = &velocities,
                .isActive = XR_FALSE,
                .jointCount = XR_HAND_JOINT_COUNT_EXT,
                .jointLocations = reinterpret_cast<XrHandJointLocationEXT*>(&handData.jointLocations[0]),
            };
            const XrHandJointsLocateInfoEXT locateInfo{
                .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
                .next = nullptr,
                .baseSpace = m_appSpace,
                .time = time
            };
            handData.isActive = XR_SUCCEEDED(m_pfnLocateHandJointsEXT(handTracker.tracker, &locateInfo, &locations)) &&
                locations.isActive == XR_TRUE;
        }
    }

    void PollHandTracking(ALXRHandTracking& handTrackingData) override {
        PollHandTracking(m_lastPredicatedDisplayTime, handTrackingData);
    }

    void PollActions() override {
        using ControllerInfo = ALXRTrackingInfo::Controller;
        constexpr static const ControllerInfo ControllerIdentity {
            .boneRotations = {},
            .bonePositionsBase = {},
            .boneRootPose {
                .orientation = { 0,0,0,1 },
                .position = { 0,0,0 },
            },
            .pose {
                .orientation = { 0,0,0,1 },
                .position = { 0,0,0 },
            },
            .angularVelocity = { 0,0,0 },
            .linearVelocity = { 0,0,0 },
            .trackpadPosition = { 0,0 },
            .buttons = 0,
            .triggerValue = 0.0f,
            .gripValue = 0.0f,
            .handFingerConfidences = 0,
            .enabled = false,
            .isHand = false,
        };
        m_input.controllerInfo = { ControllerIdentity, ControllerIdentity };
        m_interactionManager->PollActions(m_input.controllerInfo);
    }

    inline bool UseNetworkPredicatedDisplayTime() const
    {
        return !IsRuntime(OxrRuntimeType::SteamVR) &&
               !IsRuntime(OxrRuntimeType::Monado) &&
               !IsRuntime(OxrRuntimeType::SnapdragonMonado) &&
               !IsRuntime(OxrRuntimeType::AndroidXR);
    }

    inline XrCompositionLayerPassthroughFB MakeCompositionLayerPassthroughFB() const {
        return {
            .type  = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB,
            .next  = nullptr,
            .flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
            .space = XR_NULL_HANDLE,
            .layerHandle = m_ptLayerData.reconPassthroughLayer,
        };
    }

    inline XrCompositionLayerPassthroughHTC MakeCompositionLayerPassthroughHTC() const {
        static constexpr const XrPassthroughColorHTC ptColor {
            .type = XR_TYPE_PASSTHROUGH_COLOR_HTC,
            .next = nullptr,
            .alpha = 0.5f
        };
        return XrCompositionLayerPassthroughHTC {
            .type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_HTC,
            .next = nullptr,
            .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
            .space = XR_NULL_HANDLE,
            .passthrough = m_ptLayerData.passthroughHTC,
            .color = ptColor
        };
    }

    XrSteadyClock::time_point lastFrameTime = XrSteadyClock::now();

    void HeadlessWaitFrame() {
        assert(IsHeadlessSession());
        static_assert(XrSteadyClock::is_steady);
        using millisecondsf = std::chrono::duration<float, std::chrono::milliseconds::period>;
        const auto targetFrameTime = millisecondsf((1.0f / m_streamConfig.renderConfig.refreshRate) * 1000.0f);
        const auto currTime = XrSteadyClock::now();
        const auto elapsed = std::chrono::duration_cast<millisecondsf>(currTime - lastFrameTime);
        if (elapsed > targetFrameTime) {
            lastFrameTime = currTime;
            return;
        }
        const auto sleepTime = targetFrameTime - elapsed;
        std::this_thread::sleep_for(sleepTime);
        lastFrameTime = XrSteadyClock::now();
    }

    void RenderFrame() override {
        if (IsHeadlessSession()) {
            HeadlessWaitFrame();
            const auto [displayTime,ignore] = XrTimeNow();
            m_lastPredicatedDisplayTime.store(displayTime);
            PollFaceEyeTracking(displayTime);
            return;
        }
        RenderFrameImpl();
    }

    void RenderFrameImpl() {
        CHECK(m_session != XR_NULL_HANDLE);
        constexpr const XrFrameWaitInfo frameWaitInfo{
            .type = XR_TYPE_FRAME_WAIT_INFO,
            .next = nullptr
        };
        XrFrameState frameState{
            .type = XR_TYPE_FRAME_STATE,
            .next = nullptr
        };
        if (XR_FAILED(xrWaitFrame(m_session, &frameWaitInfo, &frameState))) {
            if (m_renderMode.load() == RenderMode::VideoStream) {
                m_graphicsPlugin->BeginVideoView();
                m_graphicsPlugin->EndVideoView();
            }
            return;
        }
        m_PredicatedLatencyOffset.store(frameState.predictedDisplayPeriod);
        m_lastPredicatedDisplayTime.store(frameState.predictedDisplayTime);

        PollFaceEyeTracking(frameState.predictedDisplayTime);

        const auto renderMode = m_renderMode.load();
        const bool isVideoStream = renderMode == RenderMode::VideoStream;
        std::uint64_t videoFrameDisplayTime = std::uint64_t(-1);
        if (isVideoStream) {
            m_graphicsPlugin->BeginVideoView();
            videoFrameDisplayTime = m_graphicsPlugin->GetVideoFrameIndex();
        }
        const bool timeRender = videoFrameDisplayTime != std::uint64_t(-1) &&
                                videoFrameDisplayTime != m_lastVideoFrameIndex;
        m_lastVideoFrameIndex = videoFrameDisplayTime;
        
        XrTime predictedDisplayTime;
        const auto predictedViews = GetPredicatedViews(frameState, renderMode, videoFrameDisplayTime, /*out*/ predictedDisplayTime);

        constexpr const XrFrameBeginInfo frameBeginInfo{
            .type = XR_TYPE_FRAME_BEGIN_INFO,
            .next = nullptr
        };
        if (XR_FAILED(xrBeginFrame(m_session, &frameBeginInfo))) {
            if (isVideoStream)
                m_graphicsPlugin->EndVideoView();
            return;
        }

        XrCompositionLayerPassthroughFB  passthroughLayer;
        XrCompositionLayerPassthroughHTC passthroughLayerHTC;
        XrCompositionLayerProjection     layer;
        std::uint32_t layerCount = 0;
        std::array<const XrCompositionLayerBaseHeader*, 2> layers{};
        std::array<XrCompositionLayerProjectionView,2> projectionLayerViews;
        const auto currentEnvBlendMode = m_environmentBlendMode.load();
        if (frameState.shouldRender == XR_TRUE)
        {
            XrCompositionLayerFlags ptRenderLayerFlags = 0;
            const auto passthroughMode = m_currentPTMode.load();
            if (passthroughMode != ALXR::PassthroughMode::None) {
                if (m_ptLayerData.reconPassthroughLayer != XR_NULL_HANDLE) {
                    passthroughLayer = MakeCompositionLayerPassthroughFB();
                    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&passthroughLayer);
                    ptRenderLayerFlags = XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                }
                if (m_ptLayerData.passthroughHTC != XR_NULL_HANDLE) {
                    passthroughLayerHTC = MakeCompositionLayerPassthroughHTC();
                    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&passthroughLayerHTC);
                    ptRenderLayerFlags = XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                }
                if (currentEnvBlendMode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) {
                    ptRenderLayerFlags = XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                }
            }
            const std::span<const XrView> views { predictedViews.begin(), predictedViews.end() };
            if (RenderLayer(predictedDisplayTime, views, projectionLayerViews, layer, passthroughMode)) {
                layer.layerFlags |= ptRenderLayerFlags;
                layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
            }
        }

        if (timeRender)
            LatencyCollector::Instance().rendered2(videoFrameDisplayTime);

        const XrFrameEndInfo frameEndInfo{
            .type = XR_TYPE_FRAME_END_INFO,
            .next = &xrLocalDimmingFrameEndInfoMETA,
            // TODO: Figure out why steamvr doesn't like using custom predicated display times!!!
            .displayTime = UseNetworkPredicatedDisplayTime() ?
                predictedDisplayTime : frameState.predictedDisplayTime,
            //.displayTime = frameState.predictedDisplayTime,
            .environmentBlendMode = currentEnvBlendMode,
            .layerCount = layerCount,
            .layers = layers.data()
        };
        if (XR_FAILED(xrEndFrame(m_session, &frameEndInfo))) {
            Log::Write(Log::Level::Verbose, "xrEndFrame failed!");
        }

        LatencyManager::Instance().SubmitAndSync(videoFrameDisplayTime, !timeRender);
        if (isVideoStream)
            m_graphicsPlugin->EndVideoView();
        
        if (m_delayOnGuardianChanged)
        {
            m_delayOnGuardianChanged = false;
            enqueueGuardianChanged();
        }
    }

    inline bool LocateViews(const XrTime predictedDisplayTime, const std::uint32_t viewCapacityInput, XrView* views) const
    {
        if (predictedDisplayTime == 0)
            return false;
        const XrViewLocateInfo viewLocateInfo{
            .type = XR_TYPE_VIEW_LOCATE_INFO,
            .next = nullptr,
            .viewConfigurationType = m_viewConfigType,
            .displayTime = predictedDisplayTime,
            .space = m_appSpace,
        };
        XrViewState viewState {
            .type = XR_TYPE_VIEW_STATE,
            .next = nullptr
        };
        uint32_t viewCountOutput = 0;
        const XrResult res = xrLocateViews(m_session, &viewLocateInfo, &viewState, viewCapacityInput, &viewCountOutput, views);
        if (XR_FAILED(res))
          return false;

        if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0 ||
            (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
            return false;  // There is no valid tracking poses for the views.
        }
        CHECK(viewCountOutput == viewCapacityInput);
#if 0
        CHECK(viewCountOutput == m_configViews.size());
        CHECK(viewCountOutput == m_swapchains.size());
#endif
        return true;
    }

    using VizCubeList = std::vector<Cube>;
    VizCubeList GetVisualizedHandCubes(const XrTime predictedDisplayTime) /*const*/ {

        if (m_pfnLocateHandJointsEXT == nullptr || predictedDisplayTime == 0)
            return {};

        VizCubeList handCubes;
        handCubes.reserve(XR_HAND_JOINT_COUNT_EXT * 2);

        for (const auto hand : { Side::LEFT,Side::RIGHT }) {

            auto& handTracker = m_input.handTrackers[hand];
            XrHandJointLocationsEXT locations{
                .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
                .next = nullptr, //&velocities,
                .isActive = XR_FALSE,
                .jointCount = XR_HAND_JOINT_COUNT_EXT,
                .jointLocations = handTracker.jointLocations.data(),
            };
            const XrHandJointsLocateInfoEXT locateInfo{
                .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
                .next = nullptr,
                .baseSpace = m_appSpace,
                .time = predictedDisplayTime
            };
            if (XR_FAILED(m_pfnLocateHandJointsEXT(handTracker.tracker, &locateInfo, &locations)) ||
                locations.isActive == XR_FALSE)
                continue;

            const auto& jointLocations = handTracker.jointLocations;
            for (size_t jointIdx = 0; jointIdx < XR_HAND_JOINT_COUNT_EXT; ++jointIdx)
            {
                const auto& jointLoc = jointLocations[jointIdx];
                const auto& newPose = Math::Pose::IsPoseValid(jointLoc) ?
                    jointLoc.pose : ALXR::IdentityPose;
                const auto scale = ALXR::GetHandJointScale(static_cast<XrHandJointEXT>(jointIdx));
                handCubes.push_back(Cube{ newPose, scale });
            }
        }
        return handCubes;
    }

    VizCubeList GetVisualizedCubes(const XrTime predictedDisplayTime) /*const*/ {

        if (predictedDisplayTime == 0)
            return {};

        auto cubes = GetVisualizedHandCubes(predictedDisplayTime);
        const bool hasHandCubes = cubes.size() > 0;

#ifdef ALXR_ENGINE_ENABLE_VIZ_SPACES
        // For each locatable space that we want to visualize, render a 25cm cube.
        cubes.reserve(cubes.size() + m_visualizedSpaces.size() + Side::COUNT);
        for (XrSpace visualizedSpace : m_visualizedSpaces) {
            XrSpaceLocation spaceLocation{ .type = XR_TYPE_SPACE_LOCATION, .next = nullptr };
            XrResult res = xrLocateSpace(visualizedSpace, m_appSpace, predictedDisplayTime, &spaceLocation);
            CHECK_XRRESULT(res, "xrLocateSpace");
            if (XR_UNQUALIFIED_SUCCESS(res)) {
                if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
                    (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
                    cubes.push_back(Cube{ spaceLocation.pose, {0.25f, 0.25f, 0.25f} });
                }
            }
            else {
                Log::Write(Log::Level::Verbose, Fmt("Unable to locate a visualized reference space in app space: %d", res));
            }
        }
#endif
        if (hasHandCubes)
            return cubes;
        constexpr const std::array<const float, Side::COUNT> HandScale = { {1.0f, 1.0f} };
        // Render a 10cm cube scaled by grabAction for each hand. Note renderHand will only be
        // true when the application has focus.
        for (const auto hand : { Side::LEFT, Side::RIGHT }) {
            const auto spaceLocation = GetHandSpaceLocation(hand, predictedDisplayTime, ALXR::InfinitySpaceLoc);
            if (!spaceLocation.is_infinity()) {
                const float scale = 0.1f * HandScale[hand];
                cubes.push_back(Cube{ spaceLocation.pose, {scale, scale, scale} });
            }
        }
        return cubes;
    }

    inline bool RenderLayer
    (
        const XrTime predictedDisplayTime,
        const std::span<const XrView>& views,
        std::array<XrCompositionLayerProjectionView, 2>& projectionLayerViews,
        XrCompositionLayerProjection& layer,
        const ALXR::PassthroughMode mode
    ) {
        if (m_isMultiViewEnabled)
            return RenderLayerMultiView
            (
                predictedDisplayTime, views, projectionLayerViews,
                layer, mode
            );
        else
            return RenderLayerSeperateViews
            (
                predictedDisplayTime, views, projectionLayerViews,
                layer, mode
            );
    }

    static inline std::uint32_t AcquireAndWaitForSwapchainImage(const Swapchain& swapChain) {
        std::uint32_t swapchainImageIndex = 0;
        constexpr const XrSwapchainImageAcquireInfo acquireInfo{
            .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
            .next = nullptr
        };
        if (XR_FAILED(xrAcquireSwapchainImage(swapChain.handle, &acquireInfo, &swapchainImageIndex))) {
            return static_cast<const std::uint32_t>(-1);
        }

        constexpr const XrSwapchainImageWaitInfo waitInfo{
            .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
            .next = nullptr,
            .timeout = XR_INFINITE_DURATION
        };
        if (XR_FAILED(xrWaitSwapchainImage(swapChain.handle, &waitInfo))) {
            return static_cast<const std::uint32_t>(-1);
        }
        return swapchainImageIndex;
    }

    constexpr static const XrCompositionLayerFlags RenderLayerFlags =
        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
        XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;

    bool RenderLayerMultiView
    (
        const XrTime predictedDisplayTime,
        const std::span<const XrView>& views,
        std::array<XrCompositionLayerProjectionView, 2>& projectionLayerViews,
        XrCompositionLayerProjection& layer,
        const ALXR::PassthroughMode mode
    )
    {
        assert(!m_swapchains.empty());
        assert(projectionLayerViews.size() == views.size());
        assert(m_isMultiViewEnabled);        

        const bool isVideoStream = m_renderMode == RenderMode::VideoStream;
        const auto vizCubes = isVideoStream ? VizCubeList{} : GetVisualizedCubes(predictedDisplayTime);
        const auto ptMode = static_cast<const ::PassthroughMode>(mode);

        const Swapchain& viewSwapchain = m_swapchains[0];
        const XrRect2Di imageRect {
            .offset = {0, 0},
            .extent = {viewSwapchain.width, viewSwapchain.height}
        };

        const std::uint32_t swapchainImageIndex = AcquireAndWaitForSwapchainImage(viewSwapchain);
        if (swapchainImageIndex == static_cast<const std::uint32_t>(-1))
            return false;

        for (std::uint32_t viewIndex = 0; viewIndex < views.size(); ++viewIndex) {
            const auto& view = views[viewIndex];
            projectionLayerViews[viewIndex] = {
                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
                .next = nullptr,
                .pose = view.pose,
                .fov = view.fov,
                .subImage = {
                    .swapchain = viewSwapchain.handle,
                    .imageRect = imageRect,
                    .imageArrayIndex = viewIndex
                }
            };
        }

        const XrSwapchainImageBaseHeader* const swapchainImage = m_swapchainImages[viewSwapchain.handle][swapchainImageIndex];
        if (isVideoStream)
            m_graphicsPlugin->RenderVideoMultiView(projectionLayerViews, swapchainImage, m_colorSwapchainFormat, ptMode);
        else
            m_graphicsPlugin->RenderMultiView(projectionLayerViews, swapchainImage, m_colorSwapchainFormat, ptMode, vizCubes);

        constexpr const XrSwapchainImageReleaseInfo releaseInfo{
            .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
            .next = nullptr
        };
        if (XR_FAILED(xrReleaseSwapchainImage(viewSwapchain.handle, &releaseInfo)))
            return false;

        layer = XrCompositionLayerProjection{
            .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
            .next = nullptr,
            .layerFlags = RenderLayerFlags,
            .space = m_appSpace,
            .viewCount = (uint32_t)projectionLayerViews.size(),
            .views = projectionLayerViews.data()
        };
        return true;
    }

    bool RenderLayerSeperateViews
    (
        const XrTime predictedDisplayTime,
        const std::span<const XrView>& views,
        std::array<XrCompositionLayerProjectionView,2>& projectionLayerViews,
        XrCompositionLayerProjection& layer,
        const ALXR::PassthroughMode mode
    )
    {
        assert(projectionLayerViews.size() == views.size());

        const bool isVideoStream = m_renderMode == RenderMode::VideoStream;
        const auto vizCubes = isVideoStream ? VizCubeList{} : GetVisualizedCubes(predictedDisplayTime);
        const auto ptMode = static_cast<const ::PassthroughMode>(mode);

        std::array<const XrSwapchainImageBaseHeader*, 2> swapchainImages = {nullptr, nullptr};
        // Render view to the appropriate part of the swapchain image.
        for (std::uint32_t i = 0; i < views.size(); ++i) {
            // Each view has a separate swapchain which is acquired, rendered to, and released.
            const Swapchain& viewSwapchain = m_swapchains[i];
            const std::uint32_t swapchainImageIndex = AcquireAndWaitForSwapchainImage(viewSwapchain);
            if (swapchainImageIndex == static_cast<const std::uint32_t>(-1))
                return false;

            const auto& view = views[i];
            projectionLayerViews[i] = {
                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
                .next = nullptr,
                .pose = view.pose,
                .fov = view.fov,
                .subImage = {
                    .swapchain = viewSwapchain.handle,
                    .imageRect = {
                        .offset = {0, 0},
                        .extent = {viewSwapchain.width, viewSwapchain.height}
                    },
                    .imageArrayIndex = 0
                }
            };
            swapchainImages[i] = m_swapchainImages[viewSwapchain.handle][swapchainImageIndex];
        }

        if (isVideoStream)
            m_graphicsPlugin->RenderVideoView(projectionLayerViews, swapchainImages, m_colorSwapchainFormat, ptMode);
        else
            m_graphicsPlugin->RenderView(projectionLayerViews, swapchainImages, m_colorSwapchainFormat, ptMode, vizCubes);

        for (std::uint32_t i = 0; i < views.size(); ++i) {
            const Swapchain& viewSwapchain = m_swapchains[i];
            constexpr const XrSwapchainImageReleaseInfo releaseInfo{
                .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
                .next = nullptr
            };
            if (XR_FAILED(xrReleaseSwapchainImage(viewSwapchain.handle, &releaseInfo)))
                return false;
        }

        layer = XrCompositionLayerProjection {
            .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
            .next = nullptr,
            .layerFlags = RenderLayerFlags,
            .space = m_appSpace,
            .viewCount = (uint32_t)projectionLayerViews.size(),
            .views = projectionLayerViews.data()
        };
        return true;
    }

    virtual void SetRenderMode(const RenderMode newMode) override
    {
        m_renderMode = newMode;
    }

    virtual RenderMode GetRenderMode() const override {
        return m_renderMode;
    }

    float EstimateDisplayRefreshRate()
    {
#ifdef ALXR_ENABLE_ESTIMATE_DISPLAY_REFRESH_RATE
        if (m_session == XR_NULL_HANDLE)
            return 60.0f;

        using ClockType = XrSteadyClock;
        static_assert(ClockType::is_steady);
        using secondsf = std::chrono::duration<float, std::chrono::seconds::period>;

        using namespace std::literals::chrono_literals;
        constexpr const auto OneSecond = 1s;
        constexpr const size_t SamplesPerSec = 30;

        std::vector<size_t> frame_count_per_sec;
        frame_count_per_sec.reserve(SamplesPerSec);

        bool isStarted = false;
        size_t frameIdx = 0;
        auto last = XrSteadyClock::now();
        while (frame_count_per_sec.size() != SamplesPerSec) {
            bool exitRenderLoop = false, requestRestart = false;
            PollEvents(&exitRenderLoop, &requestRestart);
            if (exitRenderLoop)
                break;
            if (!IsSessionRunning())
                continue;
            if (!isStarted)
            {
                last = ClockType::now();
                isStarted = true;
            }
            XrFrameState frameState{ .type=XR_TYPE_FRAME_STATE, .next=nullptr };
            CHECK_XRCMD(xrWaitFrame(m_session, nullptr, &frameState));            
            CHECK_XRCMD(xrBeginFrame(m_session, nullptr));
            const XrFrameEndInfo frameEndInfo{
                .type = XR_TYPE_FRAME_END_INFO,
                .next = nullptr,
                .displayTime = frameState.predictedDisplayTime,
                .environmentBlendMode = m_environmentBlendMode,
                .layerCount = 0,
                .layers = nullptr
            };
            CHECK_XRCMD(xrEndFrame(m_session, &frameEndInfo));

            if (!frameState.shouldRender)
                continue;
            
            ++frameIdx;
            auto curr = ClockType::now();
            if ((curr - last) >= OneSecond)
            {
                Log::Write(Log::Level::Info, Fmt("Frame Count at %d = %d frames", frame_count_per_sec.size(), frameIdx));
                frame_count_per_sec.push_back(frameIdx);
                last = curr;
                frameIdx = 0;              
            }
        }

        const float dom = static_cast<float>(std::accumulate(frame_count_per_sec.begin(), frame_count_per_sec.end(), size_t(0)));
        const float result = dom == 0 ? 60.0f : (dom / static_cast<float>(SamplesPerSec));
        Log::Write(Log::Level::Info, Fmt("Estimated display refresh rate: %f Hz", result));
        return result;
#else
        return 90.0f;
#endif
    }

    void UpdateSupportedDisplayRefreshRates()
    {
        if (m_pfnGetDisplayRefreshRateFB) {
            CHECK_XRCMD(m_pfnGetDisplayRefreshRateFB(m_session, &m_streamConfig.renderConfig.refreshRate));
        }

        if (m_pfnEnumerateDisplayRefreshRatesFB) {
            std::uint32_t size = 0;
            CHECK_XRCMD(m_pfnEnumerateDisplayRefreshRatesFB(m_session, 0, &size, nullptr));
            assert(size > 0);
            m_displayRefreshRates.resize(size);
            CHECK_XRCMD(m_pfnEnumerateDisplayRefreshRatesFB(m_session, size, &size, m_displayRefreshRates.data()));
            return;
        }
        // If OpenXR runtime does not support XR_FB_display_refresh_rate extension
        // and currently core spec has no method of query the supported refresh rates
        // the only way to determine this is with a dumy loop
#ifdef ALXR_ENABLE_ESTIMATE_DISPLAY_REFRESH_RATE
        m_displayRefreshRates = { EstimateDisplayRefreshRate() };
#else
        m_displayRefreshRates = { 60.0f, 72.0f, 80.0f, 90.0f, 120.0f, 144.0f };
#endif
        assert(m_displayRefreshRates.size() > 0);
    }

    virtual bool GetSystemProperties(ALXRSystemProperties& systemProps) const override
    {
        if (m_instance == XR_NULL_HANDLE)
            return false;
        XrSystemProperties xrSystemProps = { .type=XR_TYPE_SYSTEM_PROPERTIES, .next=nullptr };
        if (XR_FAILED(xrGetSystemProperties(m_instance, m_systemId, &xrSystemProps))) {
            return false;
        }
        std::strncpy(systemProps.systemName, xrSystemProps.systemName, sizeof(systemProps.systemName));
        if (m_configViews.size() > 0)
        {
            const auto& configView = m_configViews[0];
            systemProps.recommendedEyeWidth = configView.recommendedImageRectWidth;// / 2;
            systemProps.recommendedEyeHeight = configView.recommendedImageRectHeight;
        }
        assert(m_displayRefreshRates.size() > 0);
        systemProps.refreshRates = m_displayRefreshRates.data();
        systemProps.refreshRatesCount = static_cast<std::uint32_t>(m_displayRefreshRates.size());
        systemProps.currentRefreshRate = m_displayRefreshRates.back();
        if (m_pfnGetDisplayRefreshRateFB) {
            if (XR_FAILED(m_pfnGetDisplayRefreshRateFB(m_session, &systemProps.currentRefreshRate))) {
                Log::Write(Log::Level::Warning, "Failed to obtain current refresh rate from runtime.");
            }
        }

        std::uint64_t trackingEnabledFlags = 0llu;
        if (IsHandTrackingEnabled()) {
            trackingEnabledFlags |= ALXR_TRACKING_ENABLED_HANDS;
        }
        if (IsEyeTrackingEnabled()) {
            trackingEnabledFlags |= ALXR_TRACKING_ENABLED_EYES;
        }
        if (IsFacialTrackingEnabled()) {
            trackingEnabledFlags |= ALXR_TRACKING_ENABLED_FACE;
        }
        systemProps.enabledTrackingSystemsFlags = trackingEnabledFlags;
        return true;
    }

    inline ALXR::SpaceLoc GetSpaceLocation(const XrSpace& targetSpace, const XrSpace& baseSpace, const ALXR::SpaceLoc& initLoc = ALXR::IdentitySpaceLoc) const
    {
        return ALXR::GetSpaceLocation(targetSpace, baseSpace, m_lastPredicatedDisplayTime, initLoc);
    }

    inline ALXR::SpaceLoc GetSpaceLocation(const XrSpace& targetSpace, const XrTime& time, const ALXR::SpaceLoc& initLoc = ALXR::IdentitySpaceLoc) const
    {
        return ALXR::GetSpaceLocation(targetSpace, m_appSpace, time, initLoc);
    }

    inline ALXR::SpaceLoc GetSpaceLocation(const XrSpace& targetSpace, const ALXR::SpaceLoc& initLoc = ALXR::IdentitySpaceLoc) const
    {
        return GetSpaceLocation(targetSpace, m_lastPredicatedDisplayTime, initLoc);
    }

    inline ALXR::SpaceLoc GetHandSpaceLocation(const std::size_t hand, const XrSpace& baseSpace, const ALXR::SpaceLoc& initLoc = ALXR::IdentitySpaceLoc) const
    {
        assert(m_interactionManager != nullptr);
        return m_interactionManager->GetSpaceLocation(hand, baseSpace, m_lastPredicatedDisplayTime, initLoc);
    }

    inline ALXR::SpaceLoc GetHandSpaceLocation(const std::size_t hand, const XrTime& time, const ALXR::SpaceLoc& initLoc = ALXR::IdentitySpaceLoc) const
    {
        assert(m_interactionManager != nullptr);
        return m_interactionManager->GetSpaceLocation(hand, m_appSpace, time, initLoc);
    }

    inline ALXR::SpaceLoc GetHandSpaceLocation(const std::size_t hand, const ALXR::SpaceLoc& initLoc = ALXR::IdentitySpaceLoc) const
    {
        return GetHandSpaceLocation(hand, m_lastPredicatedDisplayTime, initLoc);
    }

    inline std::array<XrView,2> GetPredicatedViews
    (
        const XrFrameState& frameState, const RenderMode renderMode, const std::uint64_t videoTimeStampNs,
        XrTime& predicateDisplayTime
    )
    {
        assert(frameState.predictedDisplayPeriod >= 0);
        const auto GetDefaultViews = [&]()-> std::array<XrView, 2> {
            LocateViews(frameState.predictedDisplayTime, (uint32_t)m_views.size(), m_views.data());
            return { m_views[0], m_views[1] };
        };
        predicateDisplayTime = frameState.predictedDisplayTime;
        if (renderMode == RenderMode::Lobby)
            return GetDefaultViews();

        std::shared_lock<std::shared_mutex> l(m_trackingFrameMapMutex);
        if (videoTimeStampNs != std::uint64_t(-1))
        {
            const auto trackingFrameItr = m_trackingFrameMap.find(videoTimeStampNs);
            if (trackingFrameItr != m_trackingFrameMap.cend()) {
                predicateDisplayTime = trackingFrameItr->second.displayTime;
                return trackingFrameItr->second.views;
            }
        }
        const auto result = m_trackingFrameMap.rbegin();
        if (result == m_trackingFrameMap.rend())
            return GetDefaultViews();
        predicateDisplayTime = result->second.displayTime;
        return result->second.views;
    }

    static inline ALXREyeInfo GetEyeInfo(const XrView& left_view, const XrView& right_view)
    {
        const Eigen::Vector3f v = ALXR::ToVector3f(right_view.pose.position) - ALXR::ToVector3f(left_view.pose.position);
        float ipd = std::fabs(v.stableNorm());
        if (ipd < 0.00001f)
            ipd = 0.063f;
        constexpr const auto ToEyeFov = [](const XrFovf& fov) -> EyeFov {
            return EyeFov {
                .left   = fov.angleLeft,
                .right  = fov.angleRight,
                .top    = fov.angleUp,
                .bottom = fov.angleDown
            };
        };
        return ALXREyeInfo {
            .eyeFov = {
                ToEyeFov(left_view.fov),
                ToEyeFov(right_view.fov)
            },
            .ipd = ipd
        };
    }

    static inline ALXREyeInfo GetEyeInfo(const std::array<XrView, 2>& views)
    {
        return GetEyeInfo(views[0], views[1]);
    }

    virtual inline bool GetEyeInfo(ALXREyeInfo& eyeInfo, const XrTime& time) const override
    {
        std::array<XrView, 2> newViews{ ALXR::IdentityView, ALXR::IdentityView };
        LocateViews(time, static_cast<const std::uint32_t>(newViews.size()), newViews.data());
        eyeInfo = GetEyeInfo(newViews[0], newViews[1]);
        return true;
    }

    virtual inline bool GetEyeInfo(ALXREyeInfo& eyeInfo) const override
    {
        return GetEyeInfo(eyeInfo, m_lastPredicatedDisplayTime);
    }

    virtual bool GetTrackingInfo(TrackingInfo& info, const bool clientPredict) /*const*/ override
    {
        info = {
            .controller = { m_input.controllerInfo[0], m_input.controllerInfo[1] },
            .mounted = true,
        };

        const XrDuration predicatedLatencyOffsetNs = m_PredicatedLatencyOffset.load();
        assert(predicatedLatencyOffsetNs >= 0);

        const auto trackingPredictionLatencyUs = LatencyCollector::Instance().getTrackingPredictionLatency();
        const auto [xrTimeStamp, timeStampNs] = XrTimeNow();
        assert(timeStampNs >= 0 && xrTimeStamp >= 0);

        const XrDuration totalLatencyOffsetNs = static_cast<XrDuration>(trackingPredictionLatencyUs * 1000) + predicatedLatencyOffsetNs;
        const auto predicatedDisplayTimeXR = xrTimeStamp + totalLatencyOffsetNs;      
        const auto predicatedDisplayTimeNs = static_cast<std::uint64_t>(timeStampNs + totalLatencyOffsetNs);

        std::array<XrView, 2> newViews { ALXR::IdentityView, ALXR::IdentityView };
        LocateViews(predicatedDisplayTimeXR, (const std::uint32_t)newViews.size(), newViews.data());
        {
            std::unique_lock<std::shared_mutex> lock(m_trackingFrameMapMutex);
            m_trackingFrameMap[predicatedDisplayTimeNs] = {
                .views       = newViews,
                .displayTime = predicatedDisplayTimeXR
            };
            if (m_trackingFrameMap.size() > MaxTrackingFrameCount)
                m_trackingFrameMap.erase(m_trackingFrameMap.begin());
        }
        info.targetTimestampNs = predicatedDisplayTimeNs;
        
        const auto hmdSpaceLoc = GetSpaceLocation(m_viewSpace, predicatedDisplayTimeXR);
        info.headPose = ToALXRPosef(hmdSpaceLoc.pose);
        // info.HeadPose_LinearVelocity    = ToALXRVector3f(hmdSpaceLoc.linearVelocity);
        // info.HeadPose_AngularVelocity   = ToALXRVector3f(hmdSpaceLoc.angularVelocity);

        const auto lastPredicatedDisplayTime = m_lastPredicatedDisplayTime.load();
        const auto& inputPredicatedTime = clientPredict ? predicatedDisplayTimeXR : lastPredicatedDisplayTime;

        for (const auto hand : { Side::LEFT, Side::RIGHT }) {
            auto& newContInfo = info.controller[hand];
            const auto spaceLoc = GetHandSpaceLocation(hand, inputPredicatedTime);

            newContInfo.pose            = ToALXRPosef(spaceLoc.pose);
            newContInfo.linearVelocity  = ToALXRVector3f(spaceLoc.linearVelocity);
            newContInfo.angularVelocity = ToALXRVector3f(spaceLoc.angularVelocity);
        }

        PollHandTrackers(inputPredicatedTime, info.controller);

        LatencyCollector::Instance().tracking(predicatedDisplayTimeNs);
        return true;
    }

    virtual inline void ApplyHapticFeedback(const ALXR::HapticsFeedback& hapticFeedback) override
    {
        assert(m_interactionManager != nullptr);
        m_interactionManager->ApplyHapticFeedback(hapticFeedback);
    }

    virtual inline void SetStreamConfig(const ALXRStreamConfig& config) override
    {
        m_streamConfigQueue.push(config);
    }

    virtual inline bool GetStreamConfig(ALXRStreamConfig& config) const override
    {
        // TODO: Check for thread sync!
        config = m_streamConfig;
        return true;
    }

    static_assert(XR_FACE_CONFIDENCE_COUNT_FB <= XR_FACE_CONFIDENCE2_COUNT_FB);
    std::array<float, XR_FACE_CONFIDENCE2_COUNT_FB> m_confidences {};

    inline void PollFaceEyeTracking(const XrTime& ptime, ALXRFacialEyePacket& newPacket)
    {
        const bool noOptions = m_options == nullptr;
        if (noOptions || m_options->IsSelected(ALXRFacialExpressionType::HTC))
        {
            for (const auto& [facialTracker, exprCount, offset] : {
                    std::make_tuple(m_facialTrackersHTC[0], XR_FACIAL_EXPRESSION_EYE_COUNT_HTC, 0),
                    std::make_tuple(m_facialTrackersHTC[1], XR_FACIAL_EXPRESSION_LIP_COUNT_HTC, XR_FACIAL_EXPRESSION_EYE_COUNT_HTC)
                })
            {
                if (facialTracker == XR_NULL_HANDLE)
                    continue;
                XrFacialExpressionsHTC xrFacialExpr{
                    .type = XR_TYPE_FACIAL_EXPRESSIONS_HTC,
                    .next = nullptr,
                    .isActive = XR_FALSE,
                    .sampleTime = ptime,
                    .expressionCount = static_cast<std::uint32_t>(exprCount),
                    .expressionWeightings = newPacket.expressionWeights + offset
                };
                if (XR_FAILED(m_xrGetFacialExpressionsHTC(facialTracker, &xrFacialExpr)))
                    continue;
                newPacket.expressionType = ALXRFacialExpressionType::HTC;
                newPacket.expressionDataSource = ALXRFaceTrackingDataSource::VisualSource;
                if (exprCount == XR_FACIAL_EXPRESSION_EYE_COUNT_HTC) {
                    newPacket.isEyeFollowingBlendshapesValid = 1;
                }
            }
        }

        if (faceTrackerFBV2_ != XR_NULL_HANDLE &&
            (noOptions || m_options->IsSelected(ALXRFacialExpressionType::FB_V2)))
        {
            const XrFaceExpressionInfo2FB expressionInfo{
                .type = XR_TYPE_FACE_EXPRESSION_INFO2_FB,
                .next = nullptr,
                .time = ptime,
            };
            XrFaceExpressionWeights2FB expressionWeights{
                .type = XR_TYPE_FACE_EXPRESSION_WEIGHTS2_FB,
                .next = nullptr,
                .weightCount = XR_FACE_EXPRESSION2_COUNT_FB,
                .weights = newPacket.expressionWeights,
                .confidenceCount = XR_FACE_CONFIDENCE2_COUNT_FB,
                .confidences = m_confidences.data(),
                .isValid = XR_FALSE,
                .isEyeFollowingBlendshapesValid = XR_FALSE,
                .dataSource = XR_FACE_TRACKING_DATA_SOURCE2_VISUAL_FB,
            };
            assert(faceTrackerFBV2_ != XR_NULL_HANDLE && m_xrGetFaceExpressionWeights2FB_ != nullptr);
            m_xrGetFaceExpressionWeights2FB_(faceTrackerFBV2_, &expressionInfo, &expressionWeights);
                
            if (expressionWeights.isValid) {
                newPacket.isEyeFollowingBlendshapesValid = static_cast<std::uint8_t>(expressionWeights.isEyeFollowingBlendshapesValid);
                newPacket.expressionType = ALXRFacialExpressionType::FB_V2;
                newPacket.expressionDataSource = static_cast<ALXRFaceTrackingDataSource>(expressionWeights.dataSource);
            }
        }

        if (faceTrackerFB_ != XR_NULL_HANDLE &&
            (noOptions || m_options->IsSelected(ALXRFacialExpressionType::FB)))
        {
            const XrFaceExpressionInfoFB expressionInfo{
                .type = XR_TYPE_FACE_EXPRESSION_INFO_FB,
                .next = nullptr,
                .time = ptime
            };
            XrFaceExpressionWeightsFB expressionWeights{
                .type = XR_TYPE_FACE_EXPRESSION_WEIGHTS_FB,
                .next = nullptr,
                .weightCount = XR_FACE_EXPRESSION_COUNT_FB,
                .weights = newPacket.expressionWeights,
                .confidenceCount = XR_FACE_CONFIDENCE_COUNT_FB,
                .confidences = m_confidences.data(),
                .status = {
                    .isValid = XR_FALSE,
                    .isEyeFollowingBlendshapesValid = XR_FALSE,
                },
            };
            assert(faceTrackerFB_ != XR_NULL_HANDLE && m_xrGetFaceExpressionWeightsFB_ != nullptr);
            m_xrGetFaceExpressionWeightsFB_(faceTrackerFB_, &expressionInfo, &expressionWeights);

            if (expressionWeights.status.isValid) {
                newPacket.isEyeFollowingBlendshapesValid = static_cast<std::uint8_t>(expressionWeights.status.isEyeFollowingBlendshapesValid);
                newPacket.expressionType = ALXRFacialExpressionType::FB;
                newPacket.expressionDataSource = ALXRFaceTrackingDataSource::VisualSource;
            }
        }

        if (m_faceTrackerANDROID != XR_NULL_HANDLE &&
            (noOptions || m_options->IsSelected(ALXRFacialExpressionType::Android)))
        {
            const XrFaceStateGetInfoANDROID expressionInfo = {
                .type = XR_TYPE_FACE_STATE_GET_INFO_ANDROID,
                .next = nullptr,
                .time = ptime
            };
            static_assert(XR_FACE_PARAMETER_COUNT_ANDROID < (sizeof(newPacket.expressionWeights) / sizeof(float)));
            XrFaceStateANDROID expressionWeights = {
                .type = XR_TYPE_FACE_STATE_ANDROID,
                .next = nullptr,
                .parametersCountOutput = XR_FACE_PARAMETER_COUNT_ANDROID,
                .parameters = newPacket.expressionWeights,
                .faceTrackingState = XR_FACE_TRACKING_STATE_TRACKING_ANDROID,
                .sampleTime = ptime,
                .isValid = XR_FALSE,
            };
            assert(m_faceTrackerANDROID != XR_NULL_HANDLE && xrGetFaceStateANDROID != nullptr);
            xrGetFaceStateANDROID(m_faceTrackerANDROID, &expressionInfo, &expressionWeights);

            if (expressionWeights.isValid) {
                newPacket.expressionType = ALXRFacialExpressionType::Android;
                newPacket.expressionDataSource = ALXRFaceTrackingDataSource::VisualSource;
            }
        }

        if (noOptions || m_options->IsSelected(ALXREyeTrackingType::ExtEyeGazeInteraction))
        {
            if (const auto spaceLocOption = m_interactionManager->GetEyeGazeSpaceLocation(m_viewSpace, ptime)) {
                const auto& spaceLoc = spaceLocOption.value();
                const bool hasValidPose = Math::Pose::IsPoseValid(spaceLoc);
                for (std::size_t idx = 0; idx < MaxEyeCount; ++idx) {
                    newPacket.isEyeGazePoseValid[idx] = hasValidPose;
                    if (hasValidPose) {
                        newPacket.eyeGazePoses[idx] = spaceLoc.pose;
                    }
                }
                newPacket.eyeTrackerType = ALXREyeTrackingType::ExtEyeGazeInteraction;
            }
        }

        if (m_eyeTrackerANDROID != XR_NULL_HANDLE &&
            (noOptions || m_options->IsSelected(ALXREyeTrackingType::AndroidAvatarEyes)))
        {
            constexpr const XrEyeANDROID XrEyeIdentity = {
                .eyeState = XR_EYE_STATE_INVALID_ANDROID,
                .eyePose = ALXR::IdentityPose,
            };
            const XrEyesGetInfoANDROID gazesInfo = {
                .type = XR_TYPE_EYES_GET_INFO_ANDROID,
                .next = nullptr,
                .baseSpace = m_viewSpace,
                .time = ptime
            };
            XrEyesANDROID eyeGazes = {
                .type = XR_TYPE_EYES_ANDROID,
                .next = nullptr,
                .eyes = {XrEyeIdentity, XrEyeIdentity},
                .mode = XR_EYE_TRACKING_MODE_BOTH_ANDROID,
            };
            assert(m_eyeTrackerANDROID != XR_NULL_HANDLE && xrGetEyesInfoANDROID != nullptr);
            xrGetEyesInfoANDROID(m_eyeTrackerANDROID, &gazesInfo, &eyeGazes);

            newPacket.eyeTrackerType = ALXREyeTrackingType::AndroidAvatarEyes;
            static_assert(XR_EYE_INDEX_LEFT_ANDROID == 0 &&
                            XR_EYE_INDEX_RIGHT_ANDROID == 1);
            for (std::size_t idx = 0; idx < MaxEyeCount; ++idx) {
                const auto& gaze = eyeGazes.eyes[idx];
                newPacket.eyeGazePoses[idx] = gaze.eyePose;
                newPacket.isEyeGazePoseValid[idx] = static_cast<std::uint8_t>(gaze.eyeState);
            }
        }

        if (eyeTrackerFB_ != XR_NULL_HANDLE &&
            (noOptions || m_options->IsSelected(ALXREyeTrackingType::FBEyeTrackingSocial)))
        {
            const XrEyeGazesInfoFB gazesInfo{
                .type = XR_TYPE_EYE_GAZES_INFO_FB,
                .next = nullptr,
                .baseSpace = m_viewSpace,
                .time = ptime
            };
            XrEyeGazesFB eyeGazes{
                .type = XR_TYPE_EYE_GAZES_FB,
                .next = nullptr
            };
            assert(eyeTrackerFB_ != XR_NULL_HANDLE && m_xrGetEyeGazesFB_ != nullptr);
            m_xrGetEyeGazesFB_(eyeTrackerFB_, &gazesInfo, &eyeGazes);

            newPacket.eyeTrackerType = ALXREyeTrackingType::FBEyeTrackingSocial;
            for (std::size_t idx = 0; idx < MaxEyeCount; ++idx) {
                const auto& gaze = eyeGazes.gaze[idx];
                newPacket.eyeGazePoses[idx] = gaze.gazePose;
                newPacket.isEyeGazePoseValid[idx] = static_cast<std::uint8_t>(gaze.isValid);
            }
        }
    }

    ALXRFacialEyePacket newFTPacket {
        .expressionType = ALXRFacialExpressionType::None,
        .eyeTrackerType = ALXREyeTrackingType::None,
        .isEyeFollowingBlendshapesValid = 0,
        .isEyeGazePoseValid { 0,0 },
        .expressionDataSource = ALXRFaceTrackingDataSource::UnknownSource,
    };
    void PollFaceEyeTracking(const XrTime& ptime)
    {
        if (ptime == 0 || m_vrcftProxyServer == nullptr ||
            !m_vrcftProxyServer->IsConnected())
            return;
        PollFaceEyeTracking(ptime, newFTPacket);
        m_vrcftProxyServer->SendAsync(newFTPacket);
    }

    virtual inline void PollFaceEyeTracking(ALXRFacialEyePacket& newPacket) override
    {
        PollFaceEyeTracking(m_lastPredicatedDisplayTime, newPacket);
    }

    void PollStreamConfigEvents()
    {
        ALXRStreamConfig newConfig;
        if (!m_streamConfigQueue.try_pop(newConfig))
            return;

        if (newConfig.trackingSpaceType != m_streamConfig.trackingSpaceType) {
            const auto IsRefSpaceTypeSupported = [this](const ALXRTrackingSpace ts) {
                const auto xrSpaceRefType = ToXrReferenceSpaceType(ts);
                const auto availSpaces = GetAvailableReferenceSpaces();
                return std::find(availSpaces.begin(), availSpaces.end(), xrSpaceRefType) != availSpaces.end();
            };
            if (IsRefSpaceTypeSupported(newConfig.trackingSpaceType)) {
                if (m_appSpace != XR_NULL_HANDLE) {
                    xrDestroySpace(m_appSpace);
                    m_appSpace = XR_NULL_HANDLE;
                }
                const auto oldTrackingSpaceName = ToTrackingSpaceName(m_streamConfig.trackingSpaceType);
                const auto newTrackingSpaceName = ToTrackingSpaceName(newConfig.trackingSpaceType);
                Log::Write(Log::Level::Info, Fmt("Changing tracking space from %s to %s", oldTrackingSpaceName, newTrackingSpaceName));
                const auto referenceSpaceCreateInfo = GetXrReferenceSpaceCreateInfo(newTrackingSpaceName);
                CHECK_XRCMD(xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &m_appSpace));

                m_streamConfig.trackingSpaceType = newConfig.trackingSpaceType;
            }
            else {
                Log::Write(Log::Level::Warning, Fmt("Tracking space %s is not supported, tracking space is not changed.", ToTrackingSpaceName(newConfig.trackingSpaceType)));
            }
        }

        auto& currRenderConfig = m_streamConfig.renderConfig;
        const auto& newRenderConfig = newConfig.renderConfig;        
        if (newRenderConfig.refreshRate != currRenderConfig.refreshRate) {
            [&]() {
                if (m_pfnRequestDisplayRefreshRateFB == nullptr) {
                    Log::Write(Log::Level::Warning, "This OpenXR runtime does not support setting the display refresh rate.");
                    return;
                }

                const auto itr = std::find(m_displayRefreshRates.begin(), m_displayRefreshRates.end(), newRenderConfig.refreshRate);
                if (itr == m_displayRefreshRates.end()) {
                    Log::Write(Log::Level::Warning, Fmt("Selected new refresh rate %f Hz is not supported, no change has been made.", newRenderConfig.refreshRate));
                    return;
                }

                Log::Write(Log::Level::Info, Fmt("Setting display refresh rate from %f Hz to %f Hz.", currRenderConfig.refreshRate, newRenderConfig.refreshRate));
                CHECK_XRCMD(m_pfnRequestDisplayRefreshRateFB(m_session, newRenderConfig.refreshRate));
                currRenderConfig.refreshRate = newRenderConfig.refreshRate;
            }();
        }

        //m_streamConfig = newConfig;
    }

    virtual inline void RequestExitSession() override
    {
        if (m_session == XR_NULL_HANDLE)
            return;
        CHECK_XRCMD(xrRequestExitSession(m_session));
    }

    virtual inline bool GetGuardianData(ALXRGuardianData& gd) /*const*/ override
    {
        gd.shouldSync = false;
        return m_guardianChangedQueue.try_pop(gd);
    }

    inline bool GetBoundingStageSpace(const XrTime& time, ALXR::SpaceLoc& space, XrExtent2Df& boundingArea) const
    {
        if (m_session == XR_NULL_HANDLE ||
            m_boundingStageSpace == XR_NULL_HANDLE)
            return false;
        if (XR_FAILED(xrGetReferenceSpaceBoundsRect(m_session, XR_REFERENCE_SPACE_TYPE_STAGE, &boundingArea)))
        {
            Log::Write(Log::Level::Info, "xrGetReferenceSpaceBoundsRect FAILED.");
            return false;
        }
        space = GetSpaceLocation(m_boundingStageSpace, time, ALXR::InfinitySpaceLoc);
        return !space.is_infinity();
    }

    inline bool GetBoundingStageSpace(const XrTime& time, ALXRGuardianData& gd) const
    {
        ALXR::SpaceLoc loc;
        XrExtent2Df boundingArea;
        if (!GetBoundingStageSpace(time, loc, boundingArea))
            return false;
        gd = {
            .areaWidth = boundingArea.width,
            .areaHeight = boundingArea.height,
            .shouldSync = true,
        };
        return true;
    }

    inline bool enqueueGuardianChanged(const XrTime& time)
    {
        Log::Write(Log::Level::Verbose, "Enqueuing guardian changed");
        ALXRGuardianData gd {
            .shouldSync = false
        };
        if (!GetBoundingStageSpace(time, gd))
            return false;
        Log::Write(Log::Level::Verbose, "Guardian changed enqueud successfully.");
        m_guardianChangedQueue.push(gd);
        return true;
    }

    bool enqueueGuardianChanged() {
        return enqueueGuardianChanged(m_lastPredicatedDisplayTime);
    }

    virtual inline void Resume() override {}

    virtual inline void Pause() override {}

    virtual inline std::shared_ptr<const IGraphicsPlugin> GetGraphicsPlugin() const override {
        return m_graphicsPlugin;
    }
    virtual inline std::shared_ptr<IGraphicsPlugin> GetGraphicsPlugin() override {
        return m_graphicsPlugin;
    }

   private:
    const std::shared_ptr<Options> m_options;
    std::shared_ptr<IPlatformPlugin> m_platformPlugin;
    std::shared_ptr<IGraphicsPlugin> m_graphicsPlugin;
    XrInstance m_instance{XR_NULL_HANDLE};
    XrSession m_session{XR_NULL_HANDLE};
    XrSpace m_appSpace{XR_NULL_HANDLE};
    XrSpace m_boundingStageSpace{ XR_NULL_HANDLE };
    XrSpace m_viewSpace{ XR_NULL_HANDLE };
    XrFormFactor m_formFactor{XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    XrViewConfigurationType m_viewConfigType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
    XrEnvironmentBlendModeList m_availableBlendModes {};
    std::atomic<XrEnvironmentBlendMode> m_environmentBlendMode{ XR_ENVIRONMENT_BLEND_MODE_OPAQUE };
    XrSystemId m_systemId{XR_NULL_SYSTEM_ID};

    std::vector<XrViewConfigurationView> m_configViews;
    std::vector<Swapchain> m_swapchains;
    std::map<XrSwapchain, std::vector<XrSwapchainImageBaseHeader*>> m_swapchainImages;
    std::vector<XrView> m_views;
    std::int64_t m_colorSwapchainFormat{-1};
    std::atomic<RenderMode> m_renderMode{ RenderMode::Lobby };

    std::vector<XrSpace> m_visualizedSpaces;

    // Application's current lifecycle state according to the runtime
    XrSessionState m_sessionState{XR_SESSION_STATE_UNKNOWN};
    std::atomic<bool> m_sessionRunning{false};
    OxrRuntimeType m_runtimeType { OxrRuntimeType::Unknown };

    XrEventDataBuffer m_eventDataBuffer{
        .type = XR_TYPE_EVENT_DATA_BUFFER,
        .next = nullptr
    };
    ALXR::ALXRPaths  m_alxrPaths;
    
    using InteractionManagerPtr = std::unique_ptr<ALXR::InteractionManager>;
    InteractionManagerPtr m_interactionManager{ nullptr };    
    
    struct InputState
    {
        struct HandTrackerData
        {
            std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> jointLocations;
            //std::array<XrHandJointVelocityEXT, XR_HAND_JOINT_COUNT_EXT> jointVelocities;
            Eigen::Quaternionf baseOrientation;
            XrHandTrackerEXT tracker{ XR_NULL_HANDLE };
        };
        std::array<HandTrackerData, Side::COUNT> handTrackers;
        std::array<ALXRTrackingInfo::Controller, Side::COUNT> controllerInfo{};
    };
    InputState m_input{};

    std::once_flag m_startPassthroughOnce{};
    struct PassthroughLayerData
    {
        XrPassthroughFB passthrough = XR_NULL_HANDLE;
        XrPassthroughHTC passthroughHTC = XR_NULL_HANDLE;
        XrPassthroughLayerFB reconPassthroughLayer = XR_NULL_HANDLE;
    };
    PassthroughLayerData m_ptLayerData {};

#ifdef XR_USE_PLATFORM_WIN32
    // XR_KHR_win32_convert_performance_counter_time
    PFN_xrConvertTimeToWin32PerformanceCounterKHR m_pfnConvertTimeToWin32PerformanceCounterKHR = nullptr;
    PFN_xrConvertWin32PerformanceCounterToTimeKHR m_pfnConvertWin32PerformanceCounterToTimeKHR = nullptr;
#endif
    // XR_KHR_convert_timespec_time
    PFN_xrConvertTimespecTimeToTimeKHR  m_pfnConvertTimespecTimeToTimeKHR = nullptr;
    PFN_xrConvertTimeToTimespecTimeKHR  m_pfnConvertTimeToTimespecTimeKHR = nullptr;
    
    // XR_FB_color_space
    PFN_xrEnumerateColorSpacesFB m_pfnEnumerateColorSpacesFB = nullptr;
    PFN_xrSetColorSpaceFB        m_pfnSetColorSpaceFB = nullptr;

    // XR_EXT_hand_tracking fun pointers.
    PFN_xrCreateHandTrackerEXT  m_pfnCreateHandTrackerEXT = nullptr;
    PFN_xrLocateHandJointsEXT   m_pfnLocateHandJointsEXT = nullptr;
    PFN_xrDestroyHandTrackerEXT m_pfnDestroyHandTrackerEXT = nullptr;

    // XR_FB_display_refresh_rate fun pointers.
    PFN_xrEnumerateDisplayRefreshRatesFB m_pfnEnumerateDisplayRefreshRatesFB = nullptr;
    PFN_xrGetDisplayRefreshRateFB m_pfnGetDisplayRefreshRateFB = nullptr;
    PFN_xrRequestDisplayRefreshRateFB m_pfnRequestDisplayRefreshRateFB = nullptr;

    // XR_FB_PASSTHROUGH_EXTENSION_NAME fun pointers.
    PFN_xrCreatePassthroughFB m_pfnCreatePassthroughFB = nullptr;
    PFN_xrDestroyPassthroughFB m_pfnDestroyPassthroughFB = nullptr;
    PFN_xrPassthroughStartFB m_pfnPassthroughStartFB = nullptr;
    PFN_xrPassthroughPauseFB m_pfnPassthroughPauseFB = nullptr;
    PFN_xrCreatePassthroughLayerFB m_pfnCreatePassthroughLayerFB = nullptr;
    PFN_xrDestroyPassthroughLayerFB m_pfnDestroyPassthroughLayerFB = nullptr;
    PFN_xrPassthroughLayerSetStyleFB m_pfnPassthroughLayerSetStyleFB = nullptr;
    PFN_xrPassthroughLayerPauseFB m_pfnPassthroughLayerPauseFB = nullptr;
    PFN_xrPassthroughLayerResumeFB m_pfnPassthroughLayerResumeFB = nullptr;

    // XR_HTC_PASSTHROUGH_EXTENSION fun pointers.
    PFN_xrCreatePassthroughHTC  m_pfnCreatePassthroughHTC = nullptr;
    PFN_xrDestroyPassthroughHTC m_pfnDestroyPassthroughHTC = nullptr;

#ifdef XR_USE_OXR_PICO
    PFN_xrInvokeFunctionsPICO m_pfnInvokeFunctionsPICO = nullptr;
#endif

    const XrLocalDimmingFrameEndInfoMETA xrLocalDimmingFrameEndInfoMETA;

    std::atomic<XrTime>      m_lastPredicatedDisplayTime{ 0 };

/// Tracking Thread State ////////////////////////////////////////////////////////
    struct TrackingFrame {
        std::array<XrView, 2> views;
        XrTime                displayTime;
    };
    using TrackingFrameMap = std::map<std::uint64_t, TrackingFrame>;
    mutable std::shared_mutex m_trackingFrameMapMutex;        
    TrackingFrameMap          m_trackingFrameMap{};
    std::atomic<XrDuration>   m_PredicatedLatencyOffset{ 0 };
    std::uint64_t             m_lastVideoFrameIndex = std::uint64_t(-1);
    static constexpr const std::size_t MaxTrackingFrameCount = 360 * 3;
/// End Tracking Thread State ////////////////////////////////////////////////////

    std::vector<float> m_displayRefreshRates;
    ALXRStreamConfig m_streamConfig {
        .trackingSpaceType = ALXRTrackingSpace::LocalRefSpace,
        .renderConfig {
            .refreshRate = 90.0f,
            .enableFoveation = false
        }
    };

    using StreamConfigQueue     = xrconcurrency::concurrent_queue<ALXRStreamConfig>;
    using GuardianChangedQueue  = xrconcurrency::concurrent_queue<ALXRGuardianData>;
    StreamConfigQueue    m_streamConfigQueue;
    GuardianChangedQueue m_guardianChangedQueue;
    bool                 m_delayOnGuardianChanged = false;
    bool                 m_isMultiViewEnabled = false;
};
}  // namespace

std::shared_ptr<IOpenXrProgram> CreateOpenXrProgram(const std::shared_ptr<Options>& options,
                                                    const std::shared_ptr<IPlatformPlugin>& platformPlugin,
                                                    const std::shared_ptr<IGraphicsPlugin>& graphicsPlugin) {
    return std::make_shared<OpenXrProgram>(options, platformPlugin, graphicsPlugin);
}

std::shared_ptr<IOpenXrProgram> CreateOpenXrProgram(const std::shared_ptr<Options>& options,
                                                    const std::shared_ptr<IPlatformPlugin>& platformPlugin) {
    return std::make_shared<OpenXrProgram>(options, platformPlugin);
}
