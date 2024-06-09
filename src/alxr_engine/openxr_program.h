// Copyright (c) 2017-2022, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>
#include <tuple>

struct ALXRStreamConfig;
struct ALXRSystemProperties;
struct ALXRGuardianData;
struct ALXREyeInfo;
struct ALXRFacialEyePacket;
struct ALXRHandTracking;
struct TrackingInfo;

namespace ALXR {;
struct ALXRPaths;
struct HapticsFeedback;
}

enum class AndroidThreadType : std::int32_t {
    AppMain        = 1,
    AppWorker      = 2,
    RendererMain   = 3,
    RendererWorker = 4
};

struct IOpenXrProgram {
    virtual ~IOpenXrProgram() = default;

    // Create an Instance and other basic instance-level initialization.
    virtual void CreateInstance() = 0;

    // Select a System for the view configuration specified in the Options and initialize the graphics device for the selected
    // system.
    virtual void InitializeSystem(const ALXR::ALXRPaths& alxrPaths) = 0;

    // Create a Session and other basic session-level initialization.
    virtual void InitializeSession() = 0;

    // Create a Swapchain which requires coordinating with the graphics plugin to select the format, getting the system graphics
    // properties, getting the view configuration and grabbing the resulting swapchain images.
    virtual void CreateSwapchains(const std::uint32_t eyeWidth = 0, const std::uint32_t eyeHeight = 0) = 0;

    // Process any events in the event queue.
    virtual void PollEvents(bool* exitRenderLoop, bool* requestRestart) = 0;

    // Manage session lifecycle to track if RenderFrame should be called.
    virtual bool IsSessionRunning() const = 0;

    // Manage session state to track if input should be processed.
    virtual bool IsSessionFocused() const = 0;

    // Sample input actions and generate haptic feedback.
    virtual void PollActions() = 0;

    virtual void PollFaceEyeTracking(ALXRFacialEyePacket& newPacket) = 0;

    virtual void PollHandTracking(ALXRHandTracking& handTrackingData) = 0;
    
    // Create and submit a frame.
    virtual void RenderFrame() = 0;

    enum class RenderMode : std::size_t
    {
        Lobby,
        VideoStream
    };
    virtual void SetRenderMode(const RenderMode) = 0;
    virtual RenderMode GetRenderMode() const = 0;

    virtual bool GetSystemProperties(ALXRSystemProperties& systemProps) const = 0;

    virtual bool GetTrackingInfo(TrackingInfo& info, const bool clientPredict) /*const*/ = 0;

    virtual void ApplyHapticFeedback(const ALXR::HapticsFeedback&) = 0;

    virtual void SetStreamConfig(const ALXRStreamConfig& config) = 0;
    virtual bool GetStreamConfig(ALXRStreamConfig& config) const = 0;

    virtual void RequestExitSession() = 0;

    virtual bool GetGuardianData(ALXRGuardianData& gd) /*const*/ = 0;

    struct HiddenAreaMesh final {
        std::vector<XrVector2f> vertices;
        std::vector<std::uint32_t> indices;
        bool IsValid() const {
            return !vertices.empty() && !indices.empty();
        }
    };
    virtual bool GetHiddenAreaMesh(size_t /*viewIdx*/, HiddenAreaMesh& /*mesh*/) const = 0;
    virtual bool GetEyeInfo(ALXREyeInfo&, const XrTime& t) const = 0;
    virtual bool GetEyeInfo(ALXREyeInfo&) const = 0;

    virtual std::shared_ptr<const IGraphicsPlugin> GetGraphicsPlugin() const = 0;
    virtual std::shared_ptr<IGraphicsPlugin> GetGraphicsPlugin() = 0;

    virtual void Pause() = 0;
    virtual void Resume() = 0;

    virtual inline bool SetAndroidAppThread(const AndroidThreadType) { return false; }

    virtual bool IsHeadlessSession() const = 0;

    virtual bool IsHandTrackingEnabled() const = 0;
    virtual bool IsFacialTrackingEnabled() const = 0;
    virtual bool IsEyeTrackingEnabled() const = 0;
};

struct Swapchain {
    XrSwapchain handle;
    int32_t width;
    int32_t height;
};

std::shared_ptr<IOpenXrProgram> CreateOpenXrProgram(const std::shared_ptr<Options>& options,
                                                    const std::shared_ptr<IPlatformPlugin>& platformPlugin,
                                                    const std::shared_ptr<IGraphicsPlugin>& graphicsPlugin);

std::shared_ptr<IOpenXrProgram> CreateOpenXrProgram(const std::shared_ptr<Options>& options,
                                                    const std::shared_ptr<IPlatformPlugin>& platformPlugin);
