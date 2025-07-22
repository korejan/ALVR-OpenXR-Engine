// Copyright (c) 2017-2024, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "pch.h"
#include "alxr_ctypes.h"
#include <filesystem>

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

inline const char* GetXrEnvironmentBlendModeStr(XrEnvironmentBlendMode environmentBlendMode) {
    switch (environmentBlendMode) {
        case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
            return "Opaque";
        case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
            return "Additive";
        case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
            return "AlphaBlend";
        default:
            throw std::invalid_argument(Fmt("Unknown environment blend mode '%s'", to_string(environmentBlendMode)));
    }
}

struct FirmwareVersion {

    std::array<std::uint32_t, 3> parts;

    constexpr inline FirmwareVersion
    (
        std::uint32_t major = 0,
        std::uint32_t minor = 0,
        std::uint32_t patch = 0
    ) : parts{ major, minor, patch } {}

    constexpr inline FirmwareVersion(const ALXRVersion& v)
    : FirmwareVersion(v.major,v.minor, v.minor){}

    constexpr inline FirmwareVersion(const FirmwareVersion&) noexcept = default;
    constexpr inline FirmwareVersion& operator=(const FirmwareVersion&) noexcept = default;

    constexpr inline std::uint32_t major() const { return parts[0]; }
    constexpr inline std::uint32_t minor() const { return parts[1]; }
    constexpr inline std::uint32_t patch() const { return parts[2]; }

    constexpr inline bool operator==(const FirmwareVersion& rhs) const {
        for (std::size_t idx = 0; idx < 3; ++idx) {
            if (parts[idx] != rhs.parts[idx])
                return false;
        }
        return true;
    }

    constexpr inline bool operator!=(const FirmwareVersion& rhs) const {
        return !(*this == rhs);
    }

    constexpr inline bool operator<(const FirmwareVersion& rhs) const {
        for (std::size_t idx = 0; idx < 2; ++idx) {
            if (parts[idx] < rhs.parts[idx])
                return true;
            if (parts[idx] != rhs.parts[idx])
                return false;
        }
        return parts[2] < rhs.parts[2];
    }

    constexpr inline bool operator<=(const FirmwareVersion& rhs) const {
        return (*this < rhs) || (*this == rhs);
    }

    constexpr inline bool operator>(const FirmwareVersion& rhs) const {
        return rhs < *this;
    }

    constexpr inline bool operator>=(const FirmwareVersion& rhs) const {
        return (*this > rhs) || (*this == rhs);
    }
};

struct Options {
    std::string GraphicsPlugin;

    std::string FormFactor{"Hmd"};

    std::string ViewConfiguration{"Stereo"};

    std::string EnvironmentBlendMode{"Opaque"};

    std::string AppSpace{"Stage"};

    FirmwareVersion firmwareVersion{};

    std::filesystem::path InternalDataPath{};

    ALXRPassthroughMode PassthroughMode = ALXRPassthroughMode::None;

    std::uint32_t FaceTrackingDataSources = ALXR_FACE_TRACKING_DATA_SOURCE_VISUAL;

    ALXRFacialExpressionType FacialTracking = ALXRFacialExpressionType::Auto;
    ALXREyeTrackingType      EyeTracking = ALXREyeTrackingType::Auto;

    XrVersion XrApiVersion = XR_API_VERSION_1_0;

    std::uint16_t TrackingServerPortNo = 49192;

    XrColorSpaceFB DisplayColorSpace = XR_COLOR_SPACE_QUEST_FB;
    bool DisableLinearizeSrgb=false;
    bool DisableSuggestedBindings = false;
    bool NoServerFramerateLock = false;
    bool NoFrameSkip = false;
    bool DisableLocalDimming = false;
    bool HeadlessSession = false;
    bool NoFTServer = false;
    bool NoPassthrough = false;
    bool NoHandTracking = false;
    bool SimulateHeadless = false;
    bool NoVisibilityMasks = false;
    bool NoMultiviewRendering = false;

    struct {
        XrFormFactor FormFactor{XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};

        XrViewConfigurationType ViewConfigType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};

        XrEnvironmentBlendMode EnvironmentBlendMode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    } Parsed;

    void ParseStrings() {
        Parsed.FormFactor = GetXrFormFactor(FormFactor);
        Parsed.ViewConfigType = GetXrViewConfigurationType(ViewConfiguration);
        Parsed.EnvironmentBlendMode = GetXrEnvironmentBlendMode(EnvironmentBlendMode);
    }

    std::array<float, 4> GetBackgroundClearColor() const {
        constexpr static const std::array<float, 4> SlateGrey{0.184313729f, 0.309803933f, 0.309803933f, 1.0f};
        constexpr static const std::array<float, 4> TransparentBlack{0.0f, 0.0f, 0.0f, 0.0f};
        constexpr static const std::array<float, 4> Black{0.0f, 0.0f, 0.0f, 1.0f};

        switch (Parsed.EnvironmentBlendMode) {
            case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
                return SlateGrey;
            case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
                return Black;
            case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
                return TransparentBlack;
            default:
                return SlateGrey;
        }
    }

    void SetEnvironmentBlendMode(XrEnvironmentBlendMode environmentBlendMode) {
        EnvironmentBlendMode = GetXrEnvironmentBlendModeStr(environmentBlendMode);
        Parsed.EnvironmentBlendMode = environmentBlendMode;
    }

    inline bool IsSelected(const ALXRFacialExpressionType t) const {
        return FacialTracking == ALXRFacialExpressionType::Auto || FacialTracking == t;
    }

    inline bool IsSelected(const ALXREyeTrackingType t) const {
        return EyeTracking == ALXREyeTrackingType::Auto || EyeTracking == t;
    }

    inline bool EnableHeadless() const {
        return HeadlessSession || SimulateHeadless;
    }

    inline bool IsFaceTrackingSourceEnabled(const std::uint32_t source) const {
        return (FaceTrackingDataSources & source) != 0;
    }

    inline bool EnableVisibilityMasks() const {
        return !(NoVisibilityMasks || EnableHeadless());
    }
};
