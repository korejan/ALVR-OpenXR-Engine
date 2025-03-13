// Copyright (c) 2017-2022, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef None // xlib...
#undef None
#endif

struct XrVisibilityMaskKHR;

namespace ALXR {
    struct FoveatedDecodeParams;

    enum class YcbcrFormat : std::uint32_t;
    enum class YcbcrModel : std::uint32_t;
    enum class YcbcrRange : std::uint32_t;
}

struct Cube {
    XrPosef Pose;
    XrVector3f Scale;
};

enum class PassthroughMode : std::size_t {
    None = 0,
    BlendLayer,
    MaskLayer,
    TypeCount,
};

// Wraps a graphics API so the main openxr program can be graphics API-independent.
struct IGraphicsPlugin {
    virtual ~IGraphicsPlugin() = default;

    // OpenXR extensions required by this graphics API.
    virtual std::vector<std::string> GetInstanceExtensions() const = 0;

    // Create an instance of this graphics api for the provided instance and systemId.
    virtual void InitializeDevice(XrInstance instance, XrSystemId systemId, const XrEnvironmentBlendMode /*newMode*/, const bool /*enableVisibilityMask*/) = 0;

    // Select the preferred swapchain format from the list of available formats.
    virtual int64_t SelectColorSwapchainFormat(const std::vector<int64_t>& runtimeFormats) const = 0;

    // Get the graphics binding header for session creation.
    virtual const XrBaseInStructure* GetGraphicsBinding() const = 0;

    // Allocate space for the swapchain image structures. These are different for each graphics API. The returned
    // pointers are valid for the lifetime of the graphics plugin.
    virtual std::vector<XrSwapchainImageBaseHeader*> AllocateSwapchainImageStructs(
        uint32_t capacity, const XrSwapchainCreateInfo& swapchainCreateInfo) = 0;

    virtual void ClearSwapchainImageStructs() {}

    // Render to a swapchain image for a projection view.
    virtual void RenderView
    (
        const std::array<XrCompositionLayerProjectionView,2>& layerViews,
        const std::array<const XrSwapchainImageBaseHeader*,2>& swapchainImages,
        const std::int64_t swapchainFormat,
        const PassthroughMode /*newMode*/,
        const std::vector<Cube>& cubes
    ) = 0;

    virtual void BeginVideoView() {}
    virtual void EndVideoView() {}

    virtual void RenderVideoView
    (
        const std::array<XrCompositionLayerProjectionView, 2>& /*layerViews*/,
        const std::array<const XrSwapchainImageBaseHeader*, 2>& /*swapchainImages*/,
        const std::int64_t /*swapchainFormat*/,
        const PassthroughMode /*newMode*/ = PassthroughMode::None
    ) {}

    virtual void RenderMultiView
    (
        const std::array<XrCompositionLayerProjectionView, 2>& /*layerViews*/,
        const XrSwapchainImageBaseHeader* /*swapchainImage*/,
        const std::int64_t /*swapchainFormat*/,
        const PassthroughMode /*newMode*/,
        const std::vector<Cube>& /*cubes*/
    ) {}

    virtual void RenderVideoMultiView
    (
        const std::array<XrCompositionLayerProjectionView, 2>& /*layerViews*/,
        const XrSwapchainImageBaseHeader* /*swapchainImage*/,
        const std::int64_t /*swapchainFormat*/,
        const PassthroughMode /*newMode*/ = PassthroughMode::None
    ) {}

    virtual bool IsMultiViewEnabled() const { return false; }

    // Get recommended number of sub-data element samples in view (recommendedSwapchainSampleCount)
    // if supported by the graphics plugin. A supported value otherwise.
    virtual uint32_t GetSupportedSwapchainSampleCount(const XrViewConfigurationView& view) {
        return view.recommendedSwapchainSampleCount;
    }

    struct CreateVideoTextureInfo final {
        std::uint32_t width;
        std::uint32_t height;
        ALXR::YcbcrFormat pixfmt;
        ALXR::YcbcrModel ycbcrModel;
        ALXR::YcbcrRange ycbcrRange;
    };
    virtual void CreateVideoTextures(const CreateVideoTextureInfo& /*create_info*/) {}
    virtual void CreateVideoTexturesD3D11VA(const CreateVideoTextureInfo& /*create_info*/) { return; }
    virtual void CreateVideoTexturesCUDA(const CreateVideoTextureInfo& /*create_info*/) { return; }
    virtual void CreateVideoTexturesMediaCodec(const CreateVideoTextureInfo& /*create_info*/) { return; }
    virtual void CreateVideoTexturesVAAPI(const CreateVideoTextureInfo& /*create_info*/) { return; }

    virtual const void* GetD3D11AVDevice() const { return nullptr;  }
    virtual void* GetD3D11AVDevice() { return nullptr; }

    virtual const void* GetD3D11VADeviceContext() const { return nullptr; }
    virtual void* GetD3D11VADeviceContext() { return nullptr; }

    struct Buffer {
        void* data = nullptr;
        std::size_t pitch = 0;
        std::size_t height = 0;
    };
    struct YUVBuffer {
        Buffer luma{};
        Buffer chroma{};
        Buffer chroma2{};
        std::uint64_t frameIndex = std::uint64_t(-1);
    };
    virtual void UpdateVideoTexture(const YUVBuffer& /*yuvBuffer*/) {}
    virtual void UpdateVideoTextureCUDA(const YUVBuffer& /*yuvBuffer*/) {}
    virtual void UpdateVideoTextureD3D11VA(const YUVBuffer& /*yuvBuffer*/) {}
    virtual void UpdateVideoTextureVAAPI(const YUVBuffer& /*yuvBuffer*/) {}

    struct MediaCodecBuffer final {
        YUVBuffer ycbcrBuffer;
        ALXR::YcbcrModel ycbcrModel;
        ALXR::YcbcrRange ycbcrRange;
    };
    virtual void UpdateVideoTextureMediaCodec(const MediaCodecBuffer& /*yuvBuffer*/) {}

    virtual void ClearVideoTextures(){};

    virtual std::uint64_t GetVideoFrameIndex() const { return std::uint64_t(-1); }

    virtual void SetEnableLinearizeRGB(const bool /*enable*/) {}

    virtual void SetFoveatedDecode(const ALXR::FoveatedDecodeParams* /*fovDecParm*/) {}

    virtual void SetCmdBufferWaitNextFrame(const bool /*enable*/) {}

    virtual void SetEnvironmentBlendMode(const XrEnvironmentBlendMode /*newMode*/) {}

    virtual void SetMaskModeParams
    (
        const XrVector3f& /*keyColour*/ = { 0.01f, 0.01f, 0.01f },
        const float /*alpha*/ = 0.3f
    ) {}

    virtual void SetBlendModeParams(const float /*alpha*/ = 0.6f) {}

    virtual bool SetVisibilityMask(uint32_t /*viewIndex*/, const struct XrVisibilityMaskKHR& /*visibilityMask*/) { return true; }
};

// Create a graphics plugin for the graphics API specified in the options.
std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin(const std::shared_ptr<struct Options>& options,
                                                      std::shared_ptr<struct IPlatformPlugin> platformPlugin);
