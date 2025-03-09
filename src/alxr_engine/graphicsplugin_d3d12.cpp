// Copyright (c) 2017-2022, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "pch.h"
#include "common.h"
#include "geometry.h"
#include "options.h"
#include "graphicsplugin.h"

#if defined(XR_USE_GRAPHICS_API_D3D12)
#include <type_traits>
#include <array>
#include <map>
#include <unordered_map>
#include <variant>
#include <thread>
#include <chrono>
#include <span>
#include "xr_eigen.h"

#include <DirectXColors.h>
#include <D3Dcompiler.h>
#include <d3d11on12.h>
#include "d3dx12.h"
#include "d3d_common.h"
#include "d3d_fence_event.h"
#include "foveation.h"
#include "xr_colorspaces.h"
#include "concurrent_queue.h"
#include "cuda/WindowsSecurityAttributes.h"
#ifdef XR_ENABLE_CUDA_INTEROP
#include "cuda/d3d12cuda_interop.h"
#endif

using namespace Microsoft::WRL;
using namespace DirectX;

namespace {
void InitializeD3D12DeviceForAdapter(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL minimumFeatureLevel, ID3D12Device** device) {
#if !defined(NDEBUG)
    ComPtr<ID3D12Debug> debugCtrl;
    if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), &debugCtrl))) {
        debugCtrl->EnableDebugLayer();
    }
#endif

    // ID3D12Device2 is required for view-instancing support.
    for (const auto d3d12DeviceUuid : { __uuidof(ID3D12Device2), __uuidof(ID3D12Device) }) {
        if (SUCCEEDED(D3D12CreateDevice(adapter, minimumFeatureLevel, d3d12DeviceUuid, reinterpret_cast<void**>(device)))) {
            return;
        }
    }
    CHECK_MSG(false, "Failed to create D3D12Device.");
}

constexpr inline DXGI_FORMAT MapFormat(const ALXR::YcbcrFormat pixfmt) {
    switch (pixfmt) {
    case ALXR::YcbcrFormat::NV12: return DXGI_FORMAT_NV12;
    case ALXR::YcbcrFormat::P010LE: return DXGI_FORMAT_P010;
    }
    return DXGI_FORMAT_UNKNOWN;
}

constexpr inline DXGI_FORMAT GetLumaFormat(const ALXR::YcbcrFormat yuvFmt) {
    switch (yuvFmt) {
    case ALXR::YcbcrFormat::G8_B8_R8_3PLANE_420:          return DXGI_FORMAT_R8_UNORM;
    case ALXR::YcbcrFormat::G10X6_B10X6_R10X6_3PLANE_420: return DXGI_FORMAT_R16_UNORM;
    }
    return ALXR::GetLumaFormat(MapFormat(yuvFmt));
}

constexpr inline DXGI_FORMAT GetChromaFormat(const ALXR::YcbcrFormat yuvFmt) {
    switch (yuvFmt) {
    case ALXR::YcbcrFormat::G8_B8_R8_3PLANE_420:          return DXGI_FORMAT_R8G8_UNORM;
    case ALXR::YcbcrFormat::G10X6_B10X6_R10X6_3PLANE_420: return DXGI_FORMAT_R16G16_UNORM;
    }
    return ALXR::GetChromaFormat(MapFormat(yuvFmt));
}

constexpr inline DXGI_FORMAT GetChromaUFormat(const ALXR::YcbcrFormat yuvFmt) {
    switch (yuvFmt) {
    case ALXR::YcbcrFormat::G8_B8_R8_3PLANE_420:          return DXGI_FORMAT_R8_UNORM;
    case ALXR::YcbcrFormat::G10X6_B10X6_R10X6_3PLANE_420: return DXGI_FORMAT_R16_UNORM;
    }
    return GetChromaFormat(yuvFmt);
}

constexpr inline DXGI_FORMAT GetChromaVFormat(const ALXR::YcbcrFormat yuvFmt) {
    switch (yuvFmt) {
    case ALXR::YcbcrFormat::G8_B8_R8_3PLANE_420:          return DXGI_FORMAT_R8_UNORM;
    case ALXR::YcbcrFormat::G10X6_B10X6_R10X6_3PLANE_420: return DXGI_FORMAT_R16_UNORM;
    }
    return GetChromaFormat(yuvFmt);
}

inline ALXR::YcbcrInfoRootConstant MakeYcbcrInfo(const IGraphicsPlugin::CreateVideoTextureInfo & info) {
    const XMMATRIX ycbcrToNonLinearSRGB = ALXR::LoadXrMatrix(
        ALXR::MakeYcbcrDequantizeColorMatrix(
            info.pixfmt,
            info.ycbcrModel,
            info.ycbcrRange
        ).value_or(Eigen::Matrix4f::Identity())
    );
    DirectX::XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, ycbcrToNonLinearSRGB);
    return { .dequantizeColorMatrix = m };
}

template <uint32_t alignment>
constexpr inline uint32_t AlignTo(uint32_t n) {
    static_assert((alignment & (alignment - 1)) == 0, "The alignment must be power-of-two");
    return (n + alignment - 1) & ~(alignment - 1);
}

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* d3d12Device, uint32_t size, D3D12_HEAP_TYPE heapType) {
    D3D12_RESOURCE_STATES d3d12ResourceState;
    if (heapType == D3D12_HEAP_TYPE_UPLOAD) {
        d3d12ResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
        size = AlignTo<D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>(size);
    } else {
        d3d12ResourceState = D3D12_RESOURCE_STATE_COMMON;
    }

    const D3D12_HEAP_PROPERTIES heapProp {
        .Type = heapType,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN
    };
    const D3D12_RESOURCE_DESC buffDesc{
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc {
            .Count = 1,
            .Quality = 0
        },
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE
    };
    ComPtr<ID3D12Resource> buffer;
    CHECK_HRCMD(d3d12Device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &buffDesc, d3d12ResourceState, nullptr,
                                                     __uuidof(ID3D12Resource),
                                                     reinterpret_cast<void**>(buffer.ReleaseAndGetAddressOf())));
    return buffer;
}

ComPtr<ID3D12Resource> CreateTexture2D
(
    ID3D12Device* d3d12Device, 
    const std::size_t width, const std::size_t height, const DXGI_FORMAT pixfmt,
    const D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
    const D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE,
    const std::size_t arraySize = 1
)
{
    const D3D12_RESOURCE_DESC textureDesc {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = 0,
        .Width = width,
        .Height = static_cast<UINT>(height),
        .DepthOrArraySize = static_cast<UINT16>(arraySize),
        .MipLevels = 1,
        .Format = pixfmt,
        .SampleDesc {
            .Count = 1,
            .Quality = 0
        },
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = flags,
    };
    const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> newTexture;
    CHECK_HRCMD(d3d12Device->CreateCommittedResource(&heapProps,
        heap_flags,
        &textureDesc,
        D3D12_RESOURCE_STATE_COMMON,//D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&newTexture)));
    return newTexture;
}

ComPtr<ID3D12Resource> CreateTextureUploadBuffer
(
    ID3D12Device* d3d12Device, const ComPtr<ID3D12Resource>& texture,
    const std::uint32_t firstSubResource = 0,
    const std::uint32_t numSubResources = 1
)
{
    const std::uint64_t uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), firstSubResource, numSubResources);

    const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto buffDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ComPtr<ID3D12Resource> textureUploadHeap;
    CHECK_HRCMD(d3d12Device->CreateCommittedResource(&heapProps,
        D3D12_HEAP_FLAG_NONE, &buffDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&textureUploadHeap)));
    return textureUploadHeap;
}

struct SwapchainImageContext {

    using FoveatedDecodeParamsPtr = std::shared_ptr<ALXR::FoveatedDecodeParams>;
    DXGI_FORMAT color_format{ DXGI_FORMAT_UNKNOWN };

    std::vector<XrSwapchainImageBaseHeader*> Create
    (
        ID3D12Device* d3d12Device, std::uint64_t colorFmt, const std::uint32_t capacity, const std::uint32_t viewProjbufferSize,
        const FoveatedDecodeParamsPtr fdParamPtr = nullptr // don't pass by ref.
    ) {
        m_d3d12Device = d3d12Device;
        color_format = static_cast<DXGI_FORMAT>(colorFmt);

        m_swapchainImages.resize(capacity, {
            .type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR,
            .next = nullptr,
            .texture = nullptr
        });
        std::vector<XrSwapchainImageBaseHeader*> bases(capacity);
        for (uint32_t i = 0; i < capacity; ++i) {
            bases[i] = reinterpret_cast<XrSwapchainImageBaseHeader*>(&m_swapchainImages[i]);
        }

        m_viewProjectionCBuffer = CreateBuffer(m_d3d12Device, viewProjbufferSize, D3D12_HEAP_TYPE_UPLOAD);
        m_viewProjectionCBuffer->SetName(L"SwapchainImageCtx_ViewProjectionCBuffer");

        constexpr const std::uint32_t foveationParamsSize =
            AlignTo<D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>(sizeof(ALXR::FoveatedDecodeParams));
        m_foveationParamCBuffer = CreateBuffer(m_d3d12Device, foveationParamsSize, D3D12_HEAP_TYPE_UPLOAD);
        m_foveationParamCBuffer->SetName(L"SwapchainImageCtx_FoveationParamCBuffer");
        if (fdParamPtr)
            SetFoveationDecodeData(*fdParamPtr);

        return bases;
    }

    uint32_t ImageIndex(const XrSwapchainImageBaseHeader* swapchainImageHeader) const {
        const auto p = reinterpret_cast<const XrSwapchainImageD3D12KHR*>(swapchainImageHeader);
        return (uint32_t)(p - &m_swapchainImages[0]);
    }

    ComPtr<ID3D12Resource> GetDepthStencilTexture
    (
        const ComPtr<ID3D12Resource>& colorTexture,
        const bool visiblityMaskEnabled,
        bool& isNewResource
    ) {
        if (!m_depthStencilTexture) {
            // This back-buffer has no corresponding depth-stencil texture, so create one with matching dimensions.
            const D3D12_RESOURCE_DESC colorDesc = colorTexture->GetDesc();
            constexpr const D3D12_HEAP_PROPERTIES heapProp {
                .Type = D3D12_HEAP_TYPE_DEFAULT,
                .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN
            };
            const D3D12_RESOURCE_DESC depthDesc{
                .Dimension = colorDesc.Dimension,
                .Alignment = colorDesc.Alignment,
                .Width = colorDesc.Width,
                .Height = colorDesc.Height,
                .DepthOrArraySize = colorDesc.DepthOrArraySize,
                .MipLevels = 1,
                .Format = visiblityMaskEnabled ?
                    DXGI_FORMAT_D32_FLOAT_S8X24_UINT : DXGI_FORMAT_R32_TYPELESS,
                .SampleDesc{.Count = 1},
                .Layout = colorDesc.Layout,
                .Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
            };
            const D3D12_CLEAR_VALUE clearValue = {
                .Format = visiblityMaskEnabled ?
                    DXGI_FORMAT_D32_FLOAT_S8X24_UINT : DXGI_FORMAT_D32_FLOAT,
                .DepthStencil = {
                    .Depth = 1.0f,
                    .Stencil = 0,
                },
            };
            if (FAILED(m_d3d12Device->CreateCommittedResource(
                &heapProp, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                __uuidof(ID3D12Resource), reinterpret_cast<void**>(m_depthStencilTexture.ReleaseAndGetAddressOf())))) {
                return nullptr;
            }

            m_depthStencilTexture->SetName(L"SwapchainImageCtx_DepthStencilTexture");
            isNewResource = true;
        }
        return m_depthStencilTexture;
    }

    void RequestModelCBuffer(const std::uint32_t requiredSize) {
        if (!m_modelCBuffer || (requiredSize > m_modelCBuffer->GetDesc().Width)) {
            m_modelCBuffer = CreateBuffer(m_d3d12Device, requiredSize, D3D12_HEAP_TYPE_UPLOAD);
            m_modelCBuffer->SetName(L"SwapchainImageCtx_ModelCBuffer");
        }
    }

    ID3D12Resource* GetModelCBuffer() const { return m_modelCBuffer.Get(); }
    ID3D12Resource* GetViewProjectionCBuffer() const { return m_viewProjectionCBuffer.Get(); }
    ID3D12Resource* GetFoveationParamCBuffer() const {
        assert(m_foveationParamCBuffer != nullptr);
        return m_foveationParamCBuffer.Get();
    }

    void SetFoveationDecodeData(const ALXR::FoveatedDecodeParams& fdParams) {
        if (m_foveationParamCBuffer == nullptr)
            return;
        constexpr const std::size_t AlignSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        const alignas(AlignSize) ALXR::FoveatedDecodeParams fovParams = fdParams;
        {
            constexpr const D3D12_RANGE NoReadRange{ 0, 0 };
            void* data = nullptr;
            CHECK_HRCMD(m_foveationParamCBuffer->Map(0, &NoReadRange, &data));
            assert(data != nullptr);
            std::memcpy(data, &fovParams, sizeof(fovParams));
            m_foveationParamCBuffer->Unmap(0, nullptr);
        }
    }

   private:
    ID3D12Device* m_d3d12Device{nullptr};

    std::vector<XrSwapchainImageD3D12KHR> m_swapchainImages;
    ComPtr<ID3D12Resource> m_depthStencilTexture;
    ComPtr<ID3D12Resource> m_modelCBuffer;
    ComPtr<ID3D12Resource> m_viewProjectionCBuffer;
    ComPtr<ID3D12Resource> m_foveationParamCBuffer;
};

struct D3D12GraphicsPlugin final : public IGraphicsPlugin {

    using VideoPShader = ALXR::VideoPShader;
    using CoreShaders  = ALXR::CoreShaders<D3D12_SHADER_BYTECODE>;

    template < const std::size_t N >
    using ShaderByteCodeList = CoreShaders::ShaderByteCodeSpanList<N>;

    enum RootParamIndex : UINT {
        ModelTransform=0,
        ViewProjTransform,
        LumaTexture,
        ChromaTexture,
        ChromaUTexture = ChromaTexture,
        ChromaVTexture,
        YcbcrInfoParams,
        FoveatedDecodeParams,
        TypeCount
    };

    std::shared_ptr<Options> m_options{};

    D3D12GraphicsPlugin(const std::shared_ptr<Options>& opts, std::shared_ptr<IPlatformPlugin>)
    : m_options{ opts } {
        assert(m_options != nullptr);
    }

    inline ~D3D12GraphicsPlugin() override { CloseHandle(m_fenceEvent); }

    std::vector<std::string> GetInstanceExtensions() const override { return { XR_KHR_D3D12_ENABLE_EXTENSION_NAME }; }

    D3D_SHADER_MODEL GetHighestSupportedShaderModel() const {
        if (m_device == nullptr)
            return D3D_SHADER_MODEL_5_1;
        constexpr const D3D_SHADER_MODEL ShaderModels[] = {
            D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5,
            D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2,
            D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_5_1,
        };
        for (const auto sm : ShaderModels) {
            D3D12_FEATURE_DATA_SHADER_MODEL shaderModelData {
                .HighestShaderModel = sm
            };
            if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModelData, sizeof(shaderModelData)))) {
                return shaderModelData.HighestShaderModel;
            }
        }
        return D3D_SHADER_MODEL_5_1;
    }

    void CheckMultiViewSupport()
    {
        if (m_device == nullptr)
            return;

        const auto highestShaderModel = GetHighestSupportedShaderModel();
        Log::Write(Log::Level::Verbose, Fmt("Highest supported shader model: 0x%02x", highestShaderModel));

        if (!m_options->NoMultiviewRendering) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS3 options {
                .ViewInstancingTier = D3D12_VIEW_INSTANCING_TIER_NOT_SUPPORTED
            };
            ComPtr<ID3D12Device2> device2{ nullptr };
            if (SUCCEEDED(m_device.As(&device2)) && device2 != nullptr &&
                SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options, sizeof(options)))) {
                m_isMultiViewSupported =
                    highestShaderModel >= D3D_SHADER_MODEL_6_1 &&
                    options.ViewInstancingTier != D3D12_VIEW_INSTANCING_TIER_NOT_SUPPORTED;
                Log::Write(Log::Level::Verbose, Fmt("D3D12 View-instancing tier: %d", options.ViewInstancingTier));
            }
        }

        CoreShaders::Path smDir{ "SM5" };
        if (m_isMultiViewSupported) {
            Log::Write(Log::Level::Verbose, "Setting SM6 core (multi-view) shaders.");
            smDir = "multiview";
        }
        m_coreShaders = { smDir, m_options->InternalDataPath };
    }

    inline std::uint32_t GetViewProjectionBufferSize() const {
        return static_cast<std::uint32_t>(m_isMultiViewSupported ?
            sizeof(ALXR::MultiViewProjectionConstantBuffer) : sizeof(ALXR::ViewProjectionConstantBuffer));
    }

    void InitializeDevice(XrInstance instance, XrSystemId systemId, const XrEnvironmentBlendMode newMode, const bool enableVisibilityMask) override {
        PFN_xrGetD3D12GraphicsRequirementsKHR pfnGetD3D12GraphicsRequirementsKHR = nullptr;
        CHECK_XRCMD(xrGetInstanceProcAddr(instance, "xrGetD3D12GraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetD3D12GraphicsRequirementsKHR)));

        // Create the D3D12 device for the adapter associated with the system.
        XrGraphicsRequirementsD3D12KHR graphicsRequirements{ .type=XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR, .next=nullptr };
        CHECK_XRCMD(pfnGetD3D12GraphicsRequirementsKHR(instance, systemId, &graphicsRequirements));
        const ComPtr<IDXGIAdapter1> adapter = ALXR::GetAdapter(graphicsRequirements.adapterLuid);

        // Create a list of feature levels which are both supported by the OpenXR runtime and this application.
        InitializeD3D12DeviceForAdapter(adapter.Get(), graphicsRequirements.minFeatureLevel, m_device.ReleaseAndGetAddressOf());
        m_dx12deviceluid = graphicsRequirements.adapterLuid;

        m_enableVisibilityMask = enableVisibilityMask;
        CheckMultiViewSupport();
        CHECK(m_coreShaders.IsValid());
        
        const D3D12_COMMAND_QUEUE_DESC queueDesc{
            .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
            .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
        };
        CHECK_HRCMD(m_device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
            reinterpret_cast<void**>(m_cmdQueue.ReleaseAndGetAddressOf())));
        m_cmdQueue->SetName(L"MainRenderCMDQueue");

        InitializeResources();

        m_graphicsBinding.device = m_device.Get();
        m_graphicsBinding.queue = m_cmdQueue.Get();

        SetEnvironmentBlendMode(newMode);
    }

    void InitializeResources() {
        CHECK(m_device != nullptr);
        InitializeVideoTextureResources();
        {
            constexpr const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                .NumDescriptors = 2,                
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
            };
            CHECK_HRCMD(m_device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
                reinterpret_cast<void**>(m_rtvHeap.ReleaseAndGetAddressOf())));
        }
        {
            constexpr const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                .NumDescriptors = 2,                
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
            };
            CHECK_HRCMD(m_device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
                reinterpret_cast<void**>(m_dsvHeap.ReleaseAndGetAddressOf())));
        }
        {
            constexpr const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                .NumDescriptors = 6,                
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
            };
            CHECK_HRCMD(m_device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
                reinterpret_cast<void**>(m_srvHeap.ReleaseAndGetAddressOf())));
        }

        CD3DX12_DESCRIPTOR_RANGE1 texture1Range1, texture2Range1, texture3Range1;
        texture1Range1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        texture2Range1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
        texture3Range1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

        CD3DX12_ROOT_PARAMETER1  rootParams1[RootParamIndex::TypeCount];
        rootParams1[RootParamIndex::ModelTransform      ].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
        rootParams1[RootParamIndex::ViewProjTransform   ].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
        rootParams1[RootParamIndex::LumaTexture         ].InitAsDescriptorTable(1, &texture1Range1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams1[RootParamIndex::ChromaUTexture      ].InitAsDescriptorTable(1, &texture2Range1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams1[RootParamIndex::ChromaVTexture      ].InitAsDescriptorTable(1, &texture3Range1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams1[RootParamIndex::YcbcrInfoParams].InitAsConstants(sizeof(m_ycbcrInfo) / sizeof(std::uint32_t), 2, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams1[RootParamIndex::FoveatedDecodeParams].InitAsConstantBufferView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);

        constexpr const D3D12_STATIC_SAMPLER_DESC sampler {
            .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            .MipLODBias = 0,
            .MaxAnisotropy = 0,
            .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
            .BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
            .MinLOD = 0.0f,
            .MaxLOD = D3D12_FLOAT32_MAX,
            .ShaderRegister = 0,
            .RegisterSpace = 0,
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
        };
        D3D12_STATIC_SAMPLER_DESC samplers[2]{ sampler, sampler };
        samplers[1].ShaderRegister = 1;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.Init_1_1
        (
            (UINT)std::size(rootParams1),
            rootParams1,
            (UINT)std::size(samplers), samplers,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        );

        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData {
            .HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1
        };
        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

        ComPtr<ID3DBlob> rootSignatureBlob;
        ComPtr<ID3DBlob> error;
        
        CHECK_HRCMD(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion,
            rootSignatureBlob.ReleaseAndGetAddressOf(), error.ReleaseAndGetAddressOf()));

        CHECK_HRCMD(m_device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(),
            __uuidof(ID3D12RootSignature),
            reinterpret_cast<void**>(m_rootSignature.ReleaseAndGetAddressOf())));

        CHECK_HRCMD(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void**>(m_commandAllocator.ReleaseAndGetAddressOf())));
        m_commandAllocator->SetName(L"SwapchainImageCtx_CmdAllocator");
        
        ComPtr<ID3D12GraphicsCommandList> cmdList;
        CHECK_HRCMD(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void**>(cmdList.ReleaseAndGetAddressOf())));

        ComPtr<ID3D12Resource> cubeVertexBufferUpload;
        m_cubeVertexBuffer = CreateBuffer(m_device.Get(), sizeof(Geometry::c_cubeVertices), D3D12_HEAP_TYPE_DEFAULT);
        {
            cubeVertexBufferUpload = CreateBuffer(m_device.Get(), sizeof(Geometry::c_cubeVertices), D3D12_HEAP_TYPE_UPLOAD);

            void* data = nullptr;
            const D3D12_RANGE readRange{ 0, 0 };
            CHECK_HRCMD(cubeVertexBufferUpload->Map(0, &readRange, &data));
            memcpy(data, Geometry::c_cubeVertices, sizeof(Geometry::c_cubeVertices));
            cubeVertexBufferUpload->Unmap(0, nullptr);

            cmdList->CopyBufferRegion(m_cubeVertexBuffer.Get(), 0, cubeVertexBufferUpload.Get(), 0,
                sizeof(Geometry::c_cubeVertices));
        }

        ComPtr<ID3D12Resource> cubeIndexBufferUpload;
        m_cubeIndexBuffer = CreateBuffer(m_device.Get(), sizeof(Geometry::c_cubeIndices), D3D12_HEAP_TYPE_DEFAULT);
        {
            cubeIndexBufferUpload = CreateBuffer(m_device.Get(), sizeof(Geometry::c_cubeIndices), D3D12_HEAP_TYPE_UPLOAD);

            void* data = nullptr;
            const D3D12_RANGE readRange{ 0, 0 };
            CHECK_HRCMD(cubeIndexBufferUpload->Map(0, &readRange, &data));
            memcpy(data, Geometry::c_cubeIndices, sizeof(Geometry::c_cubeIndices));
            cubeIndexBufferUpload->Unmap(0, nullptr);

            cmdList->CopyBufferRegion(m_cubeIndexBuffer.Get(), 0, cubeIndexBufferUpload.Get(), 0, sizeof(Geometry::c_cubeIndices));
        }

        CHECK_HRCMD(cmdList->Close());
        ID3D12CommandList* cmdLists[] = { cmdList.Get() };
        m_cmdQueue->ExecuteCommandLists((UINT)std::size(cmdLists), cmdLists);

        CHECK_HRCMD(m_device->CreateFence(m_frameFenceValue.load(), D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
            reinterpret_cast<void**>(m_fence.ReleaseAndGetAddressOf())));
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        CHECK(m_fenceEvent != nullptr);

        WaitForGpu();
    }

    void InitializeVideoTextureResources() {

        m_texRendereComplete.CreateFence(m_device, D3D12_FENCE_FLAG_SHARED);

        constexpr const D3D12_COMMAND_QUEUE_DESC queueDesc {
            .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,//D3D12_COMMAND_LIST_TYPE_COPY,
            .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE            
        };
        CHECK_HRCMD(m_device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
            reinterpret_cast<void**>(m_videoTexCmdCpyQueue.ReleaseAndGetAddressOf())));
        m_videoTexCmdCpyQueue->SetName(L"VideoTextureCpyQueue");

        m_texCopy.CreateFence(m_device, D3D12_FENCE_FLAG_SHARED);

        assert(m_videoTexCmdAllocator == nullptr);
        CHECK_HRCMD(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT/*COPY*/, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void**>(m_videoTexCmdAllocator.ReleaseAndGetAddressOf())));

        InitD3D11OnD3D12();
#ifdef XR_ENABLE_CUDA_INTEROP
        InitCuda();
#endif
    }

    int64_t SelectColorSwapchainFormat(const std::vector<int64_t>& runtimeFormats) const override {
        // List of supported color swapchain formats, ordered by preference.
        constexpr const DXGI_FORMAT SupportedColorSwapchainFormats[] = {
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM,
        };
        for (const auto acceptedFormat : SupportedColorSwapchainFormats) {
            const auto swapchainFormatIt = std::find(runtimeFormats.begin(), runtimeFormats.end(), acceptedFormat);
            if (swapchainFormatIt != runtimeFormats.end()) {
                assert(acceptedFormat == *swapchainFormatIt);
                return acceptedFormat;
            }
        }
        return 0;
    }

    const XrBaseInStructure* GetGraphicsBinding() const override {
        return reinterpret_cast<const XrBaseInStructure*>(&m_graphicsBinding);
    }

    std::vector<XrSwapchainImageBaseHeader*> AllocateSwapchainImageStructs(
        uint32_t capacity, const XrSwapchainCreateInfo& swapchainCreateInfo) override {
        // Allocate and initialize the buffer of image structs (must be sequential in memory for xrEnumerateSwapchainImages).
        // Return back an array of pointers to each swapchain image struct so the consumer doesn't need to know the type/size.
        auto newSwapchainImageContext = std::make_shared<SwapchainImageContext>();
        m_swapchainImageContexts.push_back(newSwapchainImageContext);
        SwapchainImageContext& swapchainImageContext = *newSwapchainImageContext;

        std::vector<XrSwapchainImageBaseHeader*> bases = swapchainImageContext.Create
        (
            m_device.Get(), swapchainCreateInfo.format, capacity, GetViewProjectionBufferSize(), m_fovDecodeParams
        );

        // Map every swapchainImage base pointer to this context
        for (auto& base : bases) {
            m_swapchainImageContextMap[base] = newSwapchainImageContext;
        }

        return bases;
    }

    virtual void ClearSwapchainImageStructs() override
    {
        m_swapchainImageContextMap.clear();
        CpuWaitForFence(m_frameFenceValue.load());
        m_swapchainImageContexts.clear();
    }

    struct PipelineStateStream final
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_VS VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK SampleMask;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_IB_STRIP_CUT_VALUE IBStripCutValue;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
        CD3DX12_PIPELINE_STATE_STREAM_NODE_MASK NodeMask;
        CD3DX12_PIPELINE_STATE_STREAM_CACHED_PSO CachedPSO;
        CD3DX12_PIPELINE_STATE_STREAM_FLAGS Flags;
        CD3DX12_PIPELINE_STATE_STREAM_VIEW_INSTANCING ViewInstancing;
    };

    template < typename PipelineStateStreamT, const std::size_t N >
    static void MakeDefaultPipelineStateDesc
    (
        PipelineStateStreamT& pipelineStateStream,
        const DXGI_FORMAT swapchainFormat,
        const std::array<D3D12_SHADER_BYTECODE, 2>& shaders,
        const std::array<const D3D12_INPUT_ELEMENT_DESC, N>& inputElementDescs,
        const bool enableVisibilityMask
    )
    {
        pipelineStateStream.VS = shaders[0];
        pipelineStateStream.PS = shaders[1];
        {
            const CD3DX12_BLEND_DESC desc(D3D12_DEFAULT);
            pipelineStateStream.BlendState = desc;
        }
        pipelineStateStream.SampleMask = 0xFFFFFFFF;
        {
            const CD3DX12_RASTERIZER_DESC desc(D3D12_DEFAULT);
            pipelineStateStream.RasterizerState = desc;
        }
        {
            CD3DX12_DEPTH_STENCIL_DESC desc(D3D12_DEFAULT);
            if (enableVisibilityMask) {
                constexpr const D3D12_DEPTH_STENCILOP_DESC stencilOpDesc = {
                    .StencilFailOp = D3D12_STENCIL_OP_KEEP,         // Keep stencil value if stencil test fails
                    .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,    // Keep stencil value if depth test fails
                    .StencilPassOp = D3D12_STENCIL_OP_KEEP,         // Keep stencil value if both tests pass
                    .StencilFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL, // Only pass if stencil value equals reference
                };
                constexpr const D3D12_DEPTH_STENCIL_DESC stencilDesc = {
                    .DepthEnable = TRUE,
                    .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
                    .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
                    .StencilEnable = TRUE,
                    .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK, // Allow reading from all stencil bit
                    .StencilWriteMask = 0x00,                           // No writing to stencil buffer (read-only
                    .FrontFace = stencilOpDesc,
                    .BackFace = stencilOpDesc,
                };
                desc = CD3DX12_DEPTH_STENCIL_DESC{ stencilDesc };
            }
            pipelineStateStream.DepthStencilState = desc;
        }
        pipelineStateStream.InputLayout = { 
            inputElementDescs.size() == 0 ? nullptr : inputElementDescs.data(),
            (UINT)inputElementDescs.size()
        };
        pipelineStateStream.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
        pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineStateStream.DSVFormat = enableVisibilityMask ?
            DXGI_FORMAT_D32_FLOAT_S8X24_UINT : DXGI_FORMAT_D32_FLOAT;
        pipelineStateStream.SampleDesc = { 1, 0 };
        pipelineStateStream.NodeMask = 0;
        pipelineStateStream.CachedPSO = { nullptr, 0 };
        pipelineStateStream.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        if constexpr (std::is_same<PipelineStateStreamT, PipelineStateStream>::value) {
            constexpr static const std::array<const D3D12_VIEW_INSTANCE_LOCATION, 2> ViewInstanceLocations {
                D3D12_VIEW_INSTANCE_LOCATION {
                    .ViewportArrayIndex = 0u,
                    .RenderTargetArrayIndex = 0u
                },
                D3D12_VIEW_INSTANCE_LOCATION {
                    .ViewportArrayIndex = 0u,
                    .RenderTargetArrayIndex = 1u
                },
            };
            pipelineStateStream.RTVFormats = D3D12_RT_FORMAT_ARRAY{
                .RTFormats { swapchainFormat },
                .NumRenderTargets = 1u,
            };
            pipelineStateStream.ViewInstancing = CD3DX12_VIEW_INSTANCING_DESC
            (
                (UINT)ViewInstanceLocations.size(),
                ViewInstanceLocations.data(),
                D3D12_VIEW_INSTANCING_FLAG_NONE
            );
        } else {
            pipelineStateStream.NumRenderTargets = 1u;
            pipelineStateStream.RTVFormats[0] = swapchainFormat;
        }
    }

    template < const std::size_t N >
    inline ComPtr<ID3D12PipelineState> MakePipelineState
    (
        const DXGI_FORMAT swapchainFormat,
        const std::array<D3D12_SHADER_BYTECODE, 2>& shaders,
        const std::array<const D3D12_INPUT_ELEMENT_DESC, N>& inputElementDescs
    ) const {
        assert(m_device != nullptr);
        ComPtr<ID3D12PipelineState> pipelineState{ nullptr };
        if (m_isMultiViewSupported) {
            ComPtr<ID3D12Device2> device2{ nullptr };
            CHECK_HRCMD(m_device.As(&device2));
            CHECK(device2 != nullptr);

            PipelineStateStream pipelineStateDesc{ .pRootSignature = m_rootSignature.Get() };
            MakeDefaultPipelineStateDesc(pipelineStateDesc, swapchainFormat, shaders, inputElementDescs, m_enableVisibilityMask);
            const D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc{
                .SizeInBytes = sizeof(PipelineStateStream),
                .pPipelineStateSubobjectStream = &pipelineStateDesc
            };
            CHECK_HRCMD(device2->CreatePipelineState(&pipelineStateStreamDesc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void**>(pipelineState.ReleaseAndGetAddressOf())));
        }
        else {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc{ .pRootSignature = m_rootSignature.Get() };
            MakeDefaultPipelineStateDesc(pipelineStateDesc, swapchainFormat, shaders, inputElementDescs, m_enableVisibilityMask);
            CHECK_HRCMD(m_device->CreateGraphicsPipelineState(&pipelineStateDesc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void**>(pipelineState.ReleaseAndGetAddressOf())));
        }
        assert(pipelineState != nullptr);
        return pipelineState;
    }

    ID3D12PipelineState* GetOrCreateDefaultPipelineState(const DXGI_FORMAT swapchainFormat) {
        const auto iter = m_pipelineStates.find(swapchainFormat);
        if (iter != m_pipelineStates.end()) {
            return iter->second.Get();
        }

        constexpr static const std::array<const D3D12_INPUT_ELEMENT_DESC, 2> inputElementDescs = {
            D3D12_INPUT_ELEMENT_DESC {
                "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            D3D12_INPUT_ELEMENT_DESC {
                "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
        };
        const auto shaders = m_coreShaders.GetLobbyByteCodes();
        ComPtr<ID3D12PipelineState> pipelineState = MakePipelineState(swapchainFormat, shaders, inputElementDescs);

        ID3D12PipelineState* const pipelineStateRaw = pipelineState.Get();
        m_pipelineStates.emplace(swapchainFormat, std::move(pipelineState));
        return pipelineStateRaw;
    }

    constexpr static inline std::size_t VideoPipelineIndex(const bool is3PlaneFmt, const PassthroughMode newMode) {
        return static_cast<const std::size_t>(newMode) + (is3PlaneFmt ? VideoPShader::Normal3Plane : VideoPShader::Normal);
    }

    ID3D12PipelineState* GetOrCreateVideoPipelineState(const DXGI_FORMAT swapchainFormat, const PassthroughMode newMode) {
        const bool is3PlaneFormat = m_is3PlaneFormat.load();
        const auto iter = m_VideoPipelineStates.find(swapchainFormat);
        if (iter != m_VideoPipelineStates.end()) {
            return iter->second[VideoPipelineIndex(is3PlaneFormat, newMode)].Get();
        }

        constexpr static const std::array<const D3D12_INPUT_ELEMENT_DESC, 0> EmptyInputElementDescs {};
        const auto makePipeline = [&, this](const ShaderByteCodeList<2>& shaders) -> ComPtr<ID3D12PipelineState>
        {
            return MakePipelineState(swapchainFormat, shaders, EmptyInputElementDescs);
        };
        
        const auto videoShaderBCodes = m_coreShaders.GetVideoByteCodes(m_fovDecodeParams != nullptr);
        const auto newPipelineState = m_VideoPipelineStates.emplace(swapchainFormat, VidePipelineStateList{
            makePipeline({ videoShaderBCodes[0], videoShaderBCodes[1+VideoPShader::Normal] }),
            makePipeline({ videoShaderBCodes[0], videoShaderBCodes[1+VideoPShader::PassthroughBlend] }),
            makePipeline({ videoShaderBCodes[0], videoShaderBCodes[1+VideoPShader::PassthroughMask] }),
            makePipeline({ videoShaderBCodes[0], videoShaderBCodes[1+VideoPShader::Normal3Plane] }),
            makePipeline({ videoShaderBCodes[0], videoShaderBCodes[1+VideoPShader::PassthroughBlend3Plane] }),
            makePipeline({ videoShaderBCodes[0], videoShaderBCodes[1+VideoPShader::PassthroughMask3Plane] }),
        });
        CHECK(newPipelineState.second);
        return newPipelineState.first->second[VideoPipelineIndex(is3PlaneFormat, newMode)].Get();
    }

    enum class RenderPipelineType {
        Default,
        Video
    };
    inline ID3D12PipelineState* GetOrCreatePipelineState
    (
        const DXGI_FORMAT swapchainFormat,
        const RenderPipelineType pt = RenderPipelineType::Default,
        const PassthroughMode newMode = PassthroughMode::None
    ) {
        switch (pt) {
        case RenderPipelineType::Video:
            return GetOrCreateVideoPipelineState(swapchainFormat, newMode);
        case RenderPipelineType::Default:
        default: return GetOrCreateDefaultPipelineState(swapchainFormat);
        }
    }

    inline static D3D12_RENDER_TARGET_VIEW_DESC MakeRenderTargetViewDesc(const ComPtr<ID3D12Resource>& colorTexture, const std::uint64_t swapchainFormat) {
        const D3D12_RESOURCE_DESC colorTextureDesc = colorTexture->GetDesc();
        const auto viewFormat = (DXGI_FORMAT)swapchainFormat;
        if (colorTextureDesc.DepthOrArraySize > 1) {
            if (colorTextureDesc.SampleDesc.Count > 1) {
                return {
                    .Format = viewFormat,
                    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY,
                    .Texture2DMSArray = {
                        .ArraySize = colorTextureDesc.DepthOrArraySize,
                    },
                };
            } else {
                return {
                    .Format = viewFormat,
                    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY,
                    .Texture2DArray = {
                        .ArraySize = colorTextureDesc.DepthOrArraySize,
                    },
                };
            }
        } else {
            if (colorTextureDesc.SampleDesc.Count > 1) {
                return {
                    .Format = viewFormat,
                    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS,
                    .Texture2DMS = {},
                };
            } else {
                return {
                    .Format = viewFormat,
                    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                    .Texture2D = {},
                };
            }
        }
    }

    inline static D3D12_DEPTH_STENCIL_VIEW_DESC MakeDepthStencilViewDesc(const ComPtr<ID3D12Resource>& depthStencilTexture, const DXGI_FORMAT viewFormat) {
        const D3D12_RESOURCE_DESC depthStencilTextureDesc = depthStencilTexture->GetDesc();
        if (depthStencilTextureDesc.DepthOrArraySize > 1) {
            if (depthStencilTextureDesc.SampleDesc.Count > 1) {
                return {
                    .Format = viewFormat,
                    .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY,
                    .Texture2DMSArray = {
                        .ArraySize = depthStencilTextureDesc.DepthOrArraySize,
                    },
                };
            } else {
                return {
                    .Format = viewFormat,
                    .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY,
                    .Texture2DArray = {
                        .ArraySize = depthStencilTextureDesc.DepthOrArraySize,
                    },
                };
            }
        } else {
            if (depthStencilTextureDesc.SampleDesc.Count > 1) {
                return {
                    .Format = viewFormat,
                    .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS,
                    .Texture2DMS = {},
                };
            } else {
                return {
                    .Format = viewFormat,
                    .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
                    .Texture2D = {},
                };
            }
        }
    }

    struct RenderTarget final {
        CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetView{ D3D12_DEFAULT };
        CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilView{ D3D12_DEFAULT };
    };
    using DepthStencilViewList = std::array<RenderTarget, 2>;
    DepthStencilViewList CreateDepthStencilViewsFromImageArray(
        std::span<const XrSwapchainImageBaseHeader* const> swapchainImages,
        const int64_t swapchainFormat
    ) {
        assert(m_enableVisibilityMask);
        assert(swapchainImages.size() > 0 && swapchainImages.size() < 3);
        assert(m_visibilityMaskState.IsValid());

        const bool isMultiView = swapchainImages.size() == 1;

        const std::uint32_t rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        const std::uint32_t dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        std::array<RenderTarget, 2> depthStencilViews = {};
        for (std::uint32_t viewIdx = 0; viewIdx < static_cast<std::uint32_t>(depthStencilViews.size()); ++viewIdx) {
            const auto swapchainImage = swapchainImages[isMultiView ? 0 : viewIdx];
            const auto swapchainContextPtr = m_swapchainImageContextMap[swapchainImage].lock();
            if (swapchainContextPtr == nullptr)
                return {};

            auto& depthStencilView = depthStencilViews[viewIdx];
            depthStencilView = {
                .renderTargetView = {m_visibilityMaskState.rtvHeap->GetCPUDescriptorHandleForHeapStart(), (INT)viewIdx, rtvDescriptorSize},
                .depthStencilView = {m_visibilityMaskState.dsvHeap->GetCPUDescriptorHandleForHeapStart(), (INT)viewIdx, dsvDescriptorSize},
            };
            auto& swapchainContext = *swapchainContextPtr;
            ComPtr<ID3D12Resource> colorTexture = { reinterpret_cast<const XrSwapchainImageD3D12KHR*>(swapchainImage)->texture };
            assert(colorTexture != nullptr);

            bool isNewResource = false;
            ComPtr<ID3D12Resource> depthStencilTexture = swapchainContext.GetDepthStencilTexture(colorTexture, true, isNewResource);
            if (depthStencilTexture == nullptr)
                return {};

            auto depthStencilViewDesc = MakeDepthStencilViewDesc(depthStencilTexture, DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
            if (isMultiView) {
                if (depthStencilViewDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY) {
                    auto& texture2DArray = depthStencilViewDesc.Texture2DMSArray;
                    texture2DArray.FirstArraySlice = viewIdx;
                    texture2DArray.ArraySize = 1;
                } else {
                    assert(depthStencilViewDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DARRAY);
                    auto& texture2DArray = depthStencilViewDesc.Texture2DArray;
                    texture2DArray.FirstArraySlice = viewIdx;
                    texture2DArray.ArraySize = 1;
                }
            }
            m_device->CreateDepthStencilView(depthStencilTexture.Get(), &depthStencilViewDesc, depthStencilView.depthStencilView);

            auto renderTargetViewDesc = MakeRenderTargetViewDesc(colorTexture, swapchainFormat);
            if (isMultiView) {
                if (renderTargetViewDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY) {
                    auto& texture2DArray = renderTargetViewDesc.Texture2DMSArray;
                    texture2DArray.FirstArraySlice = viewIdx;
                    texture2DArray.ArraySize = 1;
                } else {
                    assert(renderTargetViewDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY);
                    auto& texture2DArray = renderTargetViewDesc.Texture2DArray;
                    texture2DArray.FirstArraySlice = viewIdx;
                    texture2DArray.ArraySize = 1;
                }
            }
            m_device->CreateRenderTargetView(colorTexture.Get(), &renderTargetViewDesc, depthStencilView.renderTargetView);
        }
        return depthStencilViews;
    }

    ComPtr<ID3D12GraphicsCommandList> RenderVisibilityMaskPassIfDirty(
        std::span<const XrSwapchainImageBaseHeader* const> swapchainImages,
        const std::array<XrCompositionLayerProjectionView, 2>& layerViews,
        const int64_t swapchainFormat
    ) {
        if (!m_enableVisibilityMask ||
            !m_visibilityMaskState.isDirty ||
            !m_visibilityMaskState.IsValid())
            return nullptr;

        const auto depthStencilViews = CreateDepthStencilViewsFromImageArray(swapchainImages, swapchainFormat);
        if (depthStencilViews[0].renderTargetView.ptr == 0)
            return nullptr;

        ComPtr<ID3D12GraphicsCommandList> cmdList;
        if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void**>(cmdList.ReleaseAndGetAddressOf())))) {
            return nullptr;
        }

        cmdList->SetPipelineState(m_visibilityMaskState.pipelineState.Get());
        cmdList->SetGraphicsRootSignature(m_rootSignature.Get());

        const bool isMultiView = swapchainImages.size() == 1;
        for (std::size_t viewIdx = 0; viewIdx < depthStencilViews.size(); ++viewIdx) {
            const auto swapchainImage = swapchainImages[isMultiView ? 0 : viewIdx];
            const auto swapchainContextPtr = m_swapchainImageContextMap[swapchainImage].lock();
            if (swapchainContextPtr == nullptr)
                continue;

            const auto& vbuff = m_visibilityMaskState.vertexBuffers[viewIdx];
            if (vbuff.vb == nullptr || vbuff.vertexCount == 0)
                continue;

            const auto& renderTarget = depthStencilViews[viewIdx];
            cmdList->OMSetRenderTargets(1, &renderTarget.renderTargetView, true, &renderTarget.depthStencilView);
            cmdList->OMSetStencilRef(1);

            const auto& imageRect = layerViews[viewIdx].subImage.imageRect;
            const D3D12_VIEWPORT viewport = {
                (float)imageRect.offset.x,     (float)imageRect.offset.y,
                (float)imageRect.extent.width, (float)imageRect.extent.height,
                0, 1
            };
            const D3D12_RECT scissorRect = {
                imageRect.offset.x,  imageRect.offset.y,
                imageRect.offset.x + imageRect.extent.width,
                imageRect.offset.y + imageRect.extent.height
            };
            cmdList->RSSetViewports(1, &viewport);
            cmdList->RSSetScissorRects(1, &scissorRect);
            cmdList->ClearDepthStencilView(renderTarget.depthStencilView, D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

            if (m_visibilityMaskState.projectionCBuffer) {
                constexpr const std::size_t AlignSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
                constexpr const std::uint32_t projPerViewSize = AlignTo<AlignSize>(sizeof(DirectX::XMFLOAT4X4A));

                const alignas(AlignSize) DirectX::XMFLOAT4X4A projection = MakeProj_XMFLOAT4X4A(layerViews[viewIdx]);
                const std::size_t offset = viewIdx * projPerViewSize;
                auto& projectCBubffer = m_visibilityMaskState.projectionCBuffer;

                constexpr const D3D12_RANGE NoReadRange{ 0, 0 };
                std::uint8_t* data = nullptr;
                if (FAILED(projectCBubffer->Map(0, &NoReadRange, reinterpret_cast<void**>(&data))) ||
                    data == nullptr)
                    continue;
                std::memcpy(data + offset, &projection, sizeof(projection));
                const D3D12_RANGE writeRange{ offset, offset + projPerViewSize };
                projectCBubffer->Unmap(0, &writeRange);
                
                cmdList->SetGraphicsRootConstantBufferView(RootParamIndex::ModelTransform, projectCBubffer->GetGPUVirtualAddress() + offset);
            }

            const D3D12_VERTEX_BUFFER_VIEW vertexBufferView[] = {
                {
                    vbuff.vb->GetGPUVirtualAddress(),
                    sizeof(XrVector2f) * vbuff.vertexCount,
                    sizeof(XrVector2f)
                }
            };
            const D3D12_INDEX_BUFFER_VIEW indexBufferView = {
                vbuff.ib->GetGPUVirtualAddress(),
                sizeof(std::uint32_t) * vbuff.indexCount,
                DXGI_FORMAT_R32_UINT
            };
            cmdList->IASetVertexBuffers(0, (UINT)std::size(vertexBufferView), vertexBufferView);
            cmdList->IASetIndexBuffer(&indexBufferView);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->DrawIndexedInstanced(vbuff.indexCount, 1, 0, 0, 0);
        }
        if (FAILED(cmdList->Close()))
            return nullptr;

        m_visibilityMaskState.isDirty = false;
        return cmdList;
    }

    template < typename RenderFun >
    inline void RenderViewImpl
    (
        const std::array<XrCompositionLayerProjectionView,2>& layerViews,
        std::span<const XrSwapchainImageBaseHeader* const> swapchainImages,
        const int64_t swapchainFormat,
        RenderFun&& renderFn,
        const RenderPipelineType pt = RenderPipelineType::Default,
        const PassthroughMode newMode = PassthroughMode::None
    )
    {
        CpuWaitForFence(m_frameFenceValue.load());
        if (FAILED(m_commandAllocator->Reset()))
            return;

        const std::uint32_t rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        const std::uint32_t dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        std::array<RenderTarget, 2> renderTargets = {};
        for (std::uint32_t viewIdx = 0; viewIdx < static_cast<std::uint32_t>(swapchainImages.size()); ++viewIdx) {
            const auto swapchainImage = swapchainImages[viewIdx];
            const auto swapchainContextPtr = m_swapchainImageContextMap[swapchainImage].lock();
            if (swapchainContextPtr == nullptr)
                continue;
            auto& renderTarget = renderTargets[viewIdx];
            renderTarget = {
                .renderTargetView = {m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), (INT)viewIdx, rtvDescriptorSize},
                .depthStencilView = {m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), (INT)viewIdx, dsvDescriptorSize},
            };
            auto& swapchainContext = *swapchainContextPtr;
            ComPtr<ID3D12Resource> colorTexture = { reinterpret_cast<const XrSwapchainImageD3D12KHR*>(swapchainImage)->texture };

            const auto renderTargetViewDesc = MakeRenderTargetViewDesc(colorTexture, swapchainFormat);
            m_device->CreateRenderTargetView(colorTexture.Get(), &renderTargetViewDesc, renderTarget.renderTargetView);

            bool isNewResource = false;
            ComPtr<ID3D12Resource> depthStencilTexture = swapchainContext.GetDepthStencilTexture(colorTexture, m_enableVisibilityMask, isNewResource);
            if (depthStencilTexture == nullptr)
                return;
            const auto dsvFormat = m_enableVisibilityMask ?
                    DXGI_FORMAT_D32_FLOAT_S8X24_UINT : DXGI_FORMAT_D32_FLOAT;
            const auto depthStencilViewDesc = MakeDepthStencilViewDesc(depthStencilTexture, dsvFormat);
            m_device->CreateDepthStencilView(depthStencilTexture.Get(), &depthStencilViewDesc, renderTarget.depthStencilView);

            if (m_enableVisibilityMask && isNewResource) {
                m_visibilityMaskState.isDirty = true;
            }
        }

        const auto vizMaskCmdList = RenderVisibilityMaskPassIfDirty(swapchainImages, layerViews, swapchainFormat);
        
        ComPtr<ID3D12GraphicsCommandList> cmdList{};
        CHECK_HRCMD(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void**>(cmdList.ReleaseAndGetAddressOf())));

        ID3D12PipelineState* const pipelineState = GetOrCreatePipelineState((DXGI_FORMAT)swapchainFormat, pt, newMode);
        cmdList->SetPipelineState(pipelineState);
        cmdList->SetGraphicsRootSignature(m_rootSignature.Get());

        assert(layerViews.size() >= swapchainImages.size());
        for (std::uint32_t viewIdx = 0; viewIdx < static_cast<std::uint32_t>(swapchainImages.size()); ++viewIdx) {
            const auto& renderTarget = renderTargets[viewIdx];
            const auto& layerView = layerViews[viewIdx];
            const auto swapchainImage = swapchainImages[viewIdx];
            const auto swapchainContextPtr = m_swapchainImageContextMap[swapchainImage].lock();
            if (swapchainContextPtr == nullptr)
                continue;
            auto& swapchainContext = *swapchainContextPtr;

            const D3D12_VIEWPORT viewport = { (float)layerView.subImage.imageRect.offset.x,
                                             (float)layerView.subImage.imageRect.offset.y,
                                             (float)layerView.subImage.imageRect.extent.width,
                                             (float)layerView.subImage.imageRect.extent.height,
                                             0,
                                             1 };
            cmdList->RSSetViewports(1, &viewport);

            const D3D12_RECT scissorRect = { layerView.subImage.imageRect.offset.x, layerView.subImage.imageRect.offset.y,
                                            layerView.subImage.imageRect.offset.x + layerView.subImage.imageRect.extent.width,
                                            layerView.subImage.imageRect.offset.y + layerView.subImage.imageRect.extent.height };
            cmdList->RSSetScissorRects(1, &scissorRect);
            if (m_enableVisibilityMask)
                cmdList->OMSetStencilRef(1);
            renderFn(viewIdx, layerView, cmdList, renderTarget.renderTargetView, renderTarget.depthStencilView, swapchainContext);
        }
        CHECK_HRCMD(cmdList->Close());

        std::uint32_t cmdListCount = 0;
        ID3D12CommandList* cmdLists[2] = {};
        if (vizMaskCmdList)
            cmdLists[cmdListCount++] = vizMaskCmdList.Get();
        cmdLists[cmdListCount++] = cmdList.Get();
        assert(cmdListCount > 0);
        m_cmdQueue->ExecuteCommandLists(cmdListCount, cmdLists);

        SignalFence();
    }

    inline std::size_t ClearColorIndex(const PassthroughMode /*ptMode*/) const {
        static_assert(ALXR::ClearColors.size() >= 3);
        static_assert(ALXR::VideoClearColors.size() >= 3);
        return m_clearColorIndex;
    }

    inline DirectX::XMMATRIX XM_CALLCONV MakeProjMatrix(const XrCompositionLayerProjectionView& layerView)
    {
        Eigen::Matrix4f projectionMatrix = ALXR::CreateProjectionFov(ALXR::GraphicsAPI::D3D, layerView.fov, 0.05f, 100.0f);
        return ALXR::LoadXrMatrix(projectionMatrix);
    }

    inline DirectX::XMFLOAT4X4A XM_CALLCONV MakeProj_XMFLOAT4X4A(const XrCompositionLayerProjectionView& layerView) {
        DirectX::XMFLOAT4X4A proj;
        XMStoreFloat4x4(&proj, MakeProjMatrix(layerView));
        return proj;
    }

    inline DirectX::XMFLOAT4X4A XM_CALLCONV MakeViewProjMatrix(const XrCompositionLayerProjectionView& layerView)
    {
        const XMMATRIX spaceToView = XMMatrixInverse(nullptr, ALXR::LoadXrPose(layerView.pose));
        DirectX::XMFLOAT4X4A viewProj;
        XMStoreFloat4x4(&viewProj, XMMatrixTranspose(spaceToView * MakeProjMatrix(layerView)));
        return viewProj;
    }

    virtual void RenderMultiView
    (
        const std::array<XrCompositionLayerProjectionView,2>& layerViews, const XrSwapchainImageBaseHeader* swapchainImage,
        const std::int64_t swapchainFormat, const PassthroughMode ptMode,
        const std::vector<Cube>& cubes
    ) override
    {
        assert(m_isMultiViewSupported);
        using CpuDescHandle = D3D12_CPU_DESCRIPTOR_HANDLE;
        using CommandListPtr = ComPtr<ID3D12GraphicsCommandList>;
        const std::array<const XrSwapchainImageBaseHeader*,1> swapchainImages = {swapchainImage};
        RenderViewImpl(layerViews, swapchainImages, swapchainFormat, [&]
        (
            const std::uint32_t viewID,
            const XrCompositionLayerProjectionView& layerView,
            const CommandListPtr& cmdList,
            const CpuDescHandle& renderTargetView,
            const CpuDescHandle& depthStencilView,
            SwapchainImageContext& swapchainContext
        )
        {
            cmdList->ClearRenderTargetView(renderTargetView, ALXR::ClearColors[ClearColorIndex(ptMode)], 0, nullptr);
            cmdList->ClearDepthStencilView(depthStencilView, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            const D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { renderTargetView };
            cmdList->OMSetRenderTargets((UINT)std::size(renderTargets), renderTargets, true, &depthStencilView);

            ALXR::MultiViewProjectionConstantBuffer viewProjection{ {} };
            for (std::size_t viewIndex = 0; viewIndex < 2; ++viewIndex) {
                viewProjection.ViewProjection[viewIndex] = MakeViewProjMatrix(layerViews[viewIndex]);
            }

            // Set shaders and constant buffers.
            ID3D12Resource* const viewProjectionCBuffer = swapchainContext.GetViewProjectionCBuffer();
            {
                constexpr const D3D12_RANGE NoReadRange{ 0, 0 };
                void* data = nullptr;
                CHECK_HRCMD(viewProjectionCBuffer->Map(0, &NoReadRange, &data));
                assert(data != nullptr);
                std::memcpy(data, &viewProjection, sizeof(viewProjection));
                viewProjectionCBuffer->Unmap(0, nullptr);
            }
            cmdList->SetGraphicsRootConstantBufferView(RootParamIndex::ViewProjTransform, viewProjectionCBuffer->GetGPUVirtualAddress());

            RenderVisCubes(cubes, swapchainContext, cmdList);

        }, RenderPipelineType::Default);
    }

    virtual void RenderView
    (
        const std::array<XrCompositionLayerProjectionView, 2>& layerViews,
        const std::array<const XrSwapchainImageBaseHeader*, 2>& swapchainImages,
        const std::int64_t swapchainFormat, const PassthroughMode ptMode,
        const std::vector<Cube>& cubes
    ) override
    {           
        using CpuDescHandle = D3D12_CPU_DESCRIPTOR_HANDLE;
        using CommandListPtr = ComPtr<ID3D12GraphicsCommandList>;
        RenderViewImpl(layerViews, swapchainImages, swapchainFormat, [&]
        (
            const std::uint32_t viewID,
            const XrCompositionLayerProjectionView& layerView,
            const CommandListPtr& cmdList,
            const CpuDescHandle& renderTargetView,
            const CpuDescHandle& depthStencilView,
            SwapchainImageContext& swapchainContext
        ) {
            assert(layerView.subImage.imageArrayIndex == 0);

            // Clear swapchain and depth buffer. NOTE: This will clear the entire render target view, not just the specified view.
            cmdList->ClearRenderTargetView(renderTargetView, ALXR::ClearColors[ClearColorIndex(ptMode)], 0, nullptr);
            cmdList->ClearDepthStencilView(depthStencilView, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            const D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { renderTargetView };
            cmdList->OMSetRenderTargets((UINT)std::size(renderTargets), renderTargets, true, &depthStencilView);

            // Set shaders and constant buffers.
            const ALXR::ViewProjectionConstantBuffer viewProjection{
                .ViewProjection = MakeViewProjMatrix(layerView),
                .ViewID = 0,
            };
            ID3D12Resource* const viewProjectionCBuffer = swapchainContext.GetViewProjectionCBuffer();
            {
                constexpr const D3D12_RANGE NoReadRange{ 0, 0 };
                void* data = nullptr;
                CHECK_HRCMD(viewProjectionCBuffer->Map(0, &NoReadRange, &data));
                assert(data != nullptr);
                std::memcpy(data, &viewProjection, sizeof(viewProjection));
                viewProjectionCBuffer->Unmap(0, nullptr);
            }
            cmdList->SetGraphicsRootConstantBufferView(RootParamIndex::ViewProjTransform, viewProjectionCBuffer->GetGPUVirtualAddress());

            RenderVisCubes(cubes, swapchainContext, cmdList);

        }, RenderPipelineType::Default);
    }

    void RenderVisCubes(const std::vector<Cube>& cubes, SwapchainImageContext& swapchainContext, const ComPtr<ID3D12GraphicsCommandList>& cmdList)
    {
        // Set cube primitive data.
        if (cubes.empty())
            return;
        assert(cmdList != nullptr);

        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView[] = {
            {m_cubeVertexBuffer->GetGPUVirtualAddress(), sizeof(Geometry::c_cubeVertices), sizeof(Geometry::Vertex)} };
        cmdList->IASetVertexBuffers(0, (UINT)std::size(vertexBufferView), vertexBufferView);

        const D3D12_INDEX_BUFFER_VIEW indexBufferView{ m_cubeIndexBuffer->GetGPUVirtualAddress(), sizeof(Geometry::c_cubeIndices),
                                                DXGI_FORMAT_R16_UINT };
        cmdList->IASetIndexBuffer(&indexBufferView);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        constexpr const std::uint32_t cubeCBufferSize = AlignTo<D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>(sizeof(ALXR::ModelConstantBuffer));
        swapchainContext.RequestModelCBuffer(static_cast<uint32_t>(cubeCBufferSize * cubes.size()));
        ID3D12Resource* const modelCBuffer = swapchainContext.GetModelCBuffer();

        // Render each cube
        std::uint32_t offset = 0;
        for (const Cube& cube : cubes) {
            // Compute and update the model transform.
            ALXR::ModelConstantBuffer model;
            XMStoreFloat4x4(&model.Model,
                XMMatrixTranspose(XMMatrixScaling(cube.Scale.x, cube.Scale.y, cube.Scale.z) * ALXR::LoadXrPose(cube.Pose)));
            {
                constexpr const D3D12_RANGE NoReadRange{ 0, 0 };
                std::uint8_t* data = nullptr;                
                CHECK_HRCMD(modelCBuffer->Map(0, &NoReadRange, reinterpret_cast<void**>(&data)));
                assert(data != nullptr);
                std::memcpy(data + offset, &model, sizeof(model));
                const D3D12_RANGE writeRange{ offset, offset + cubeCBufferSize };
                modelCBuffer->Unmap(0, &writeRange);
            }
            cmdList->SetGraphicsRootConstantBufferView(RootParamIndex::ModelTransform, modelCBuffer->GetGPUVirtualAddress() + offset);
            // Draw the cube.
            cmdList->DrawIndexedInstanced((UINT)std::size(Geometry::c_cubeIndices), 1, 0, 0, 0);
            
            offset += cubeCBufferSize;
        }
    }

    void SignalFence() {
        ++m_frameFenceValue;
        CHECK_HRCMD(m_cmdQueue->Signal(m_fence.Get(), m_frameFenceValue));
    }

    void CpuWaitForFence(uint64_t fenceValue) {
        if (m_fence->GetCompletedValue() < fenceValue) {
            CHECK_HRCMD(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent));
            const uint32_t retVal = WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
            if (retVal != WAIT_OBJECT_0) {
                CHECK_HRCMD(E_FAIL);
            }
        }
    }

    void WaitForGpu() {
        SignalFence();
        CpuWaitForFence(m_frameFenceValue);
    }

    void CreateVideoTextures
    (
        const CreateVideoTextureInfo& info,
        const bool createUploadBuffer,
        const D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE,
        const D3D12_RESOURCE_FLAGS res_flags = D3D12_RESOURCE_FLAG_NONE
    )
    {
        if (m_device == nullptr)
            return;

        ClearVideoTextures();

        CHECK(info.width % 2 == 0);

        m_ycbcrInfo = MakeYcbcrInfo(info);

        const bool is3PlaneFmt = ALXR::YcbcrPlaneCount(info.pixfmt) > 2;

        /*constexpr*/ const DXGI_FORMAT LUMA_FORMAT     = GetLumaFormat(info.pixfmt);
        /*constexpr*/ const DXGI_FORMAT CHROMA_FORMAT   = GetChromaFormat(info.pixfmt);
        /*constexpr*/ const DXGI_FORMAT CHROMAU_FORMAT  = GetChromaUFormat(info.pixfmt);
        /*constexpr*/ const DXGI_FORMAT CHROMAV_FORMAT  = GetChromaVFormat(info.pixfmt);

        const std::uint32_t descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_srvHeap->GetGPUDescriptorHandleForHeapStart());
        for (auto& videoTex : m_videoTextures)
        {
            if (!is3PlaneFmt)
            {
                videoTex.texture = ::CreateTexture2D(m_device.Get(), info.width, info.height, MapFormat(info.pixfmt), res_flags, heap_flags);
                CHECK(videoTex.texture != nullptr);
                if (createUploadBuffer) {
                    videoTex.uploadTexture = CreateTextureUploadBuffer(m_device.Get(), videoTex.texture, 0, 2);
                    CHECK(videoTex.uploadTexture != nullptr);
                }

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {
                    .Format = LUMA_FORMAT,
                    .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                    .Texture2D {
                        .MipLevels = 1,
                        .PlaneSlice = 0
                    }
                };
                m_device->CreateShaderResourceView(videoTex.texture.Get(), &srvDesc, cpuHandle);
                videoTex.lumaHandle = cpuHandle;
                videoTex.lumaGpuHandle = gpuHandle;
                cpuHandle.Offset(1, descriptorSize);
                gpuHandle.Offset(1, descriptorSize);

                srvDesc.Texture2D.PlaneSlice = 1;
                srvDesc.Format = CHROMA_FORMAT;
                m_device->CreateShaderResourceView(videoTex.texture.Get(), &srvDesc, cpuHandle);
                videoTex.chromaHandle = cpuHandle;
                videoTex.chromaGpuHandle = gpuHandle;
                cpuHandle.Offset(1, descriptorSize);
                gpuHandle.Offset(1, descriptorSize);

            }
            else
            {
                const std::size_t chromaWidth  = info.width / 2;
                const std::size_t chromaHeight = info.height / 2;
                videoTex.lumaTexture    = ::CreateTexture2D(m_device.Get(), info.width, info.height, LUMA_FORMAT);
                assert(CHROMAU_FORMAT == CHROMAV_FORMAT);
                videoTex.chromaTexture  = ::CreateTexture2D
                (
                    m_device.Get(), chromaWidth, chromaHeight, CHROMAU_FORMAT//,
                    //D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_FLAG_NONE, 2
                );
                videoTex.chromaVTexture = ::CreateTexture2D(m_device.Get(), chromaWidth, chromaHeight, CHROMAV_FORMAT);
                ///////////
                if (createUploadBuffer)
                {
                    videoTex.lumaStagingBuffer = CreateTextureUploadBuffer(m_device.Get(), videoTex.lumaTexture);
                    videoTex.chromaUStagingBuffer = CreateTextureUploadBuffer(m_device.Get(), videoTex.chromaTexture);// , 0, 2);
                    videoTex.chromaVStagingBuffer = CreateTextureUploadBuffer(m_device.Get(), videoTex.chromaVTexture);
                }

                //////////////
                // Describe and create a SRV for the texture.
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                    .Format = LUMA_FORMAT,
                    .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                    .Texture2D {
                        .MipLevels = 1,
                        .PlaneSlice = 0
                    }
                };
                m_device->CreateShaderResourceView(videoTex.lumaTexture.Get(), &srvDesc, cpuHandle);
                videoTex.lumaHandle = cpuHandle;
                videoTex.lumaGpuHandle = gpuHandle;
                cpuHandle.Offset(1, descriptorSize);
                gpuHandle.Offset(1, descriptorSize);

                srvDesc.Format = CHROMAU_FORMAT;
                m_device->CreateShaderResourceView(videoTex.chromaTexture.Get(), &srvDesc, cpuHandle);
                videoTex.chromaHandle = cpuHandle;
                videoTex.chromaGpuHandle = gpuHandle;
                cpuHandle.Offset(1, descriptorSize);
                gpuHandle.Offset(1, descriptorSize);

                srvDesc.Format = CHROMAV_FORMAT;
                m_device->CreateShaderResourceView(videoTex.chromaVTexture.Get(), &srvDesc, cpuHandle);
                videoTex.chromaVHandle = cpuHandle;
                videoTex.chromaVGpuHandle = gpuHandle;
                cpuHandle.Offset(1, descriptorSize);
                gpuHandle.Offset(1, descriptorSize);
            }
        }

        m_is3PlaneFormat = is3PlaneFmt;
    }

    virtual void CreateVideoTextures(const CreateVideoTextureInfo& info) override
    {
        CreateVideoTextures(info, true);
    }

    ComPtr<ID3D11Device> m_d3d11Device{};
    ComPtr<ID3D11DeviceContext> m_d3d11DeviceContext{};
    ComPtr<ID3D11On12Device> m_d3d11On12Device{};

    virtual const void* GetD3D11AVDevice() const override
    {
        return m_d3d11Device.Get();
    }

    virtual void* GetD3D11AVDevice() override {
        return m_d3d11Device.Get();
    }

    virtual const void* GetD3D11VADeviceContext() const override {
        return m_d3d11DeviceContext.Get();
    }

    virtual void* GetD3D11VADeviceContext() override {
        return m_d3d11DeviceContext.Get();
    }

    void InitD3D11OnD3D12()
    {
        if (m_device == nullptr)
            return;
        UINT d3d11DeviceFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT;// | D3D11_CREATE_DEVICE_BGRA_SUPPORT;//0;//
#ifndef NDEBUG
        d3d11DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        //const D3D_FEATURE_LEVEL features[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        CHECK_HRCMD(D3D11On12CreateDevice(
            m_device.Get(),
            d3d11DeviceFlags,
            nullptr,
            0,
            reinterpret_cast<IUnknown**>(m_videoTexCmdCpyQueue.GetAddressOf()),
            1,
            0,
            &m_d3d11Device,
            &m_d3d11DeviceContext,
            nullptr
        ));
        CHECK_HRCMD(m_d3d11Device.As(&m_d3d11On12Device));
        CHECK(m_d3d11Device != nullptr);
        ID3D10Multithread* pMultithread = nullptr;
        if (SUCCEEDED(m_d3d11Device->QueryInterface(__uuidof(ID3D10Multithread), (void**)&pMultithread))
            && pMultithread != nullptr) {
            pMultithread->SetMultithreadProtected(TRUE);
            pMultithread->Release();
        }
    }

    virtual void ClearVideoTextures() override
    {
        m_renderTex = std::size_t(-1);
        m_currentVideoTex = 0;
        //std::lock_guard<std::mutex> lk(m_renderMutex);
        m_texRendereComplete.WaitForGpu();
        m_videoTextures = { NV12Texture {}, NV12Texture {} };
        m_is3PlaneFormat = false;
    }

    virtual void CreateVideoTexturesD3D11VA(const CreateVideoTextureInfo& info) override
    {
        if (m_d3d11Device == nullptr)
            return;

        CHECK_MSG((info.pixfmt != ALXR::YcbcrFormat::G8_B8_R8_3PLANE_420 &&
            info.pixfmt != ALXR::YcbcrFormat::G10X6_B10X6_R10X6_3PLANE_420), "3-Planes formats are not supported!");
        
        CreateVideoTextures
        (
            info,
            false, D3D12_HEAP_FLAG_SHARED,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS
        );

        constexpr const D3D11_RESOURCE_FLAGS d3d11Flags {
            .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
            .MiscFlags = 0,
            .CPUAccessFlags = 0,            
            .StructureByteStride = 0,
        };
        const WindowsSecurityAttributes secAttr{};
        for (auto& vidTex : m_videoTextures)
        {
            CHECK_HRCMD(m_device->CreateSharedHandle(vidTex.texture.Get(), &secAttr, GENERIC_ALL, 0, &vidTex.wrappedD3D11SharedHandle));

            CHECK_HRCMD(m_d3d11On12Device->CreateWrappedResource
            (
                vidTex.texture.Get(),
                &d3d11Flags,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                IID_PPV_ARGS(&vidTex.wrappedD3D11Texture)
            ));
            CHECK(vidTex.wrappedD3D11Texture != nullptr);
        }
    }

    virtual void UpdateVideoTextureD3D11VA(const YUVBuffer& yuvBuffer)
    {
        CHECK(m_device != nullptr);
        CHECK(m_videoTexCmdAllocator != nullptr);
        CHECK(yuvBuffer.frameIndex != std::uint64_t(-1));

        WaitForAvailableBuffer();

        using TextureRes = ComPtr<ID3D12Resource>;
        //TextureRes lumaUploadBuffer{}, chromaUploadBuffer{};
        const std::size_t freeIndex = m_currentVideoTex.load();
        {
            /*const*/ auto& videoTex = m_videoTextures[freeIndex];
            videoTex.frameIndex = yuvBuffer.frameIndex;
            CHECK(videoTex.wrappedD3D11Texture != nullptr);

            ComPtr<ID3D11Texture2D> new_texture = reinterpret_cast<ID3D11Texture2D*>(yuvBuffer.luma.data);
            const auto texture_index = (UINT)reinterpret_cast<std::intptr_t>(yuvBuffer.chroma.data);
            CHECK(new_texture != nullptr);

            D3D11_TEXTURE2D_DESC desc{};//, desc2{};
            videoTex.wrappedD3D11Texture->GetDesc(&desc);

            m_d3d11On12Device->AcquireWrappedResources((ID3D11Resource**)videoTex.wrappedD3D11Texture.GetAddressOf(), 1);
            
            const D3D11_BOX sourceRegion{
                .left = 0,
                .top = 0,
                .front = 0,
                .right = desc.Width,
                .bottom = desc.Height,                
                .back = 1
            };
            m_d3d11DeviceContext->CopySubresourceRegion(videoTex.wrappedD3D11Texture.Get(), 0, 0, 0, 0, new_texture.Get(), texture_index, &sourceRegion);
            
            // Release our wrapped render target resource. Releasing 
            // transitions the back buffer resource to the state specified
            // as the OutState when the wrapped resource was created.
            m_d3d11On12Device->ReleaseWrappedResources((ID3D11Resource**)videoTex.wrappedD3D11Texture.GetAddressOf(), 1);

            // Flush to submit the 11 command list to the shared command queue.
            m_d3d11DeviceContext->Flush();
        }

        m_currentVideoTex.store((freeIndex + 1) % VideoTexCount);
        //CHECK_HRCMD(m_texCopy.Signal(m_videoTexCmdCpyQueue));        
        m_renderTex.store(freeIndex);
    }

    bool WaitForAvailableBuffer()
    {
        m_texRendereComplete.WaitForGpu();
        //Log::Write(Log::Level::Info, Fmt("render idx: %d, copy idx: %d", m_renderTex.load(), m_currentVideoTex.load()));
        //CHECK_HRCMD(m_texRendereComplete.Wait(m_videoTexCmdCpyQueue));
        return true;
    }

    virtual void UpdateVideoTexture(const YUVBuffer& yuvBuffer) override
    {
        CHECK(m_device != nullptr);
        CHECK(m_videoTexCmdAllocator != nullptr);
        CHECK(yuvBuffer.frameIndex != std::uint64_t(-1));
        
        WaitForAvailableBuffer();

        ComPtr<ID3D12GraphicsCommandList> cmdList;
        CHECK_HRCMD(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT/*COPY*/, m_videoTexCmdAllocator.Get(), nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void**>(cmdList.ReleaseAndGetAddressOf())));

        using TextureRes = ComPtr<ID3D12Resource>;
        //TextureRes lumaUploadBuffer{}, chromaUploadBuffer{};
        const std::size_t freeIndex = m_currentVideoTex.load();
        {
            auto& videoTex = m_videoTextures[freeIndex];
            videoTex.frameIndex = yuvBuffer.frameIndex;

            const bool is3PlaneFmt = yuvBuffer.chroma2.data != nullptr;
            if (!is3PlaneFmt)
            {
                CHECK(!m_is3PlaneFormat.load());
                CHECK(videoTex.texture != nullptr);
                CHECK(videoTex.uploadTexture != nullptr);
                
                const std::array<const D3D12_SUBRESOURCE_DATA,2> textureData
                {
                    D3D12_SUBRESOURCE_DATA {
                        .pData = yuvBuffer.luma.data,
                        .RowPitch = static_cast<LONG_PTR>(yuvBuffer.luma.pitch),
                        .SlicePitch = static_cast<LONG_PTR>(yuvBuffer.luma.pitch * yuvBuffer.luma.height)
                    },
                    D3D12_SUBRESOURCE_DATA {
                        .pData = yuvBuffer.chroma.data,
                        .RowPitch = static_cast<LONG_PTR>(yuvBuffer.chroma.pitch),
                        .SlicePitch = static_cast<LONG_PTR>(yuvBuffer.chroma.pitch * yuvBuffer.chroma.height),
                    }
                };
                CD3DX12_RESOURCE_BARRIER resourceBarrier =
                    CD3DX12_RESOURCE_BARRIER::Transition(videoTex.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                cmdList->ResourceBarrier(1, &resourceBarrier);

                UpdateSubresources
                (
                    cmdList.Get(), videoTex.texture.Get(), videoTex.uploadTexture.Get(),
                    0, 0, (UINT)textureData.size(), textureData.data()
                );

                resourceBarrier =
                    CD3DX12_RESOURCE_BARRIER::Transition(videoTex.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                cmdList->ResourceBarrier(1, &resourceBarrier);
            }
            else
            {
                CHECK(m_is3PlaneFormat.load());
                CHECK(videoTex.lumaTexture != nullptr);
                CHECK(videoTex.chromaTexture != nullptr);
                CHECK(videoTex.chromaVTexture != nullptr);

                const auto uploadData = [&cmdList](const TextureRes& tex, const TextureRes& uploadBuff, const Buffer& buf)
                {
                    const auto texDesc = tex->GetDesc();
                    CHECK(buf.height <= texDesc.Height);
                    const D3D12_SUBRESOURCE_DATA textureData {
                        .pData = buf.data,
                        .RowPitch = static_cast<LONG_PTR>(buf.pitch),
                        .SlicePitch = static_cast<LONG_PTR>(buf.pitch * buf.height)
                    };
                    UpdateSubresources<1>(cmdList.Get(), tex.Get(), uploadBuff.Get(), 0, 0, 1, &textureData);
                };

                std::array<CD3DX12_RESOURCE_BARRIER, 3> resourceBarriers = {
                    CD3DX12_RESOURCE_BARRIER::Transition(videoTex.lumaTexture.Get(),    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
                    CD3DX12_RESOURCE_BARRIER::Transition(videoTex.chromaTexture.Get(),  D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
                    CD3DX12_RESOURCE_BARRIER::Transition(videoTex.chromaVTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
                };
                cmdList->ResourceBarrier((UINT)resourceBarriers.size(), resourceBarriers.data());

                uploadData(videoTex.lumaTexture,    videoTex.lumaStagingBuffer,    yuvBuffer.luma);
                uploadData(videoTex.chromaTexture,  videoTex.chromaUStagingBuffer, yuvBuffer.chroma);
                uploadData(videoTex.chromaVTexture, videoTex.chromaVStagingBuffer, yuvBuffer.chroma2);;

                resourceBarriers = {
                    CD3DX12_RESOURCE_BARRIER::Transition(videoTex.lumaTexture.Get(),    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(videoTex.chromaTexture.Get(),  D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(videoTex.chromaVTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
                };
                cmdList->ResourceBarrier((UINT)resourceBarriers.size(), resourceBarriers.data());
            }
        }
        // END CMDS //////////////////////////////////////////////////////////////////////
        CHECK_HRCMD(cmdList->Close());
        ID3D12CommandList* cmdLists[] = { cmdList.Get() };
        m_videoTexCmdCpyQueue->ExecuteCommandLists((UINT)std::size(cmdLists), cmdLists);
        
        m_currentVideoTex.store((freeIndex + 1) % VideoTexCount);
        CHECK_HRCMD(m_texCopy.Signal(m_videoTexCmdCpyQueue));
        m_renderTex.store(freeIndex);
    }

    std::size_t currentTextureIdx = std::size_t(-1);

    virtual void BeginVideoView() override
    {
#if 0
#ifdef XR_ENABLE_CUDA_INTEROP
        const cudaExternalSemaphoreWaitParams externalSemaphoreWaitParams {
            .params{.fence{.value = m_texCopy.fenceValue.load()}},
            .flags = 0
        };
        if (cudaWaitExternalSemaphoresAsync(&m_m_texCopyExtSemaphore, &externalSemaphoreWaitParams, 1, videoBufferStream) != cudaSuccess)
        {
            Log::Write(Log::Level::Error, "cudaWaitExternalSemaphoresAsync failed.");
            CHECK(false);
        }
#endif
#else
        CHECK_HRCMD(m_texCopy.Wait(m_cmdQueue));
#endif
        currentTextureIdx = m_renderTex.load();
    }

    virtual void EndVideoView() override
    {
#if 0
#ifdef XR_ENABLE_CUDA_INTEROP
        const auto nextVal = m_texRendereComplete.fenceValue.load() + 1;
        const cudaExternalSemaphoreSignalParams externalSemaphoreSignalParams{
            .params{.fence{.value = nextVal}},
            .flags = 0
        };
        if (cudaSignalExternalSemaphoresAsync
            (&m_m_texRenderExtSemaphore, &externalSemaphoreSignalParams, 1, videoBufferStream) != cudaSuccess)
        {
            Log::Write(Log::Level::Error, "m_texRendereComplete cudaSignalExternalSemaphoresAsync failed.");
            CHECK(false);
        }
        m_texRendereComplete.fenceValue.store(nextVal);
#endif
#else
        CHECK_HRCMD(m_texRendereComplete.Signal(m_cmdQueue));
#endif
    }

    virtual std::uint64_t GetVideoFrameIndex() const override {
        return currentTextureIdx == std::uint64_t(-1) ?
            currentTextureIdx :
            m_videoTextures[currentTextureIdx].frameIndex;
    }

    virtual void RenderVideoMultiView
    (
        const std::array<XrCompositionLayerProjectionView, 2>& layerViews, const XrSwapchainImageBaseHeader* swapchainImage,
        const std::int64_t swapchainFormat, const PassthroughMode newMode /*= PassthroughMode::None*/
    ) override
    {
        CHECK(m_isMultiViewSupported);
        using CpuDescHandle = D3D12_CPU_DESCRIPTOR_HANDLE;
        using CommandListPtr = ComPtr<ID3D12GraphicsCommandList>;
        std::array<const XrSwapchainImageBaseHeader*, 1> swapchainImages = { swapchainImage };
        RenderViewImpl(layerViews, swapchainImages, swapchainFormat, [&]
        (
            const std::uint32_t viewID,
            const XrCompositionLayerProjectionView& layerView,
            const CommandListPtr& cmdList,
            const CpuDescHandle& renderTargetView,
            const CpuDescHandle& depthStencilView,
            SwapchainImageContext& swapchainContext
        )
        {
            if (currentTextureIdx == std::size_t(-1))
                return;
            const auto& videoTex = m_videoTextures[currentTextureIdx];
            
            ID3D12DescriptorHeap* const ppHeaps[] = { m_srvHeap.Get() };
            cmdList->SetDescriptorHeaps((UINT)std::size(ppHeaps), ppHeaps);
            cmdList->SetGraphicsRootDescriptorTable(RootParamIndex::LumaTexture, videoTex.lumaGpuHandle); // Second texture will be (texture1+1)
            cmdList->SetGraphicsRootDescriptorTable(RootParamIndex::ChromaTexture, videoTex.chromaGpuHandle);
            if (m_is3PlaneFormat)
                cmdList->SetGraphicsRootDescriptorTable(RootParamIndex::ChromaVTexture, videoTex.chromaVGpuHandle);
            cmdList->SetGraphicsRoot32BitConstants(RootParamIndex::YcbcrInfoParams, sizeof(m_ycbcrInfo) / sizeof(std::uint32_t),
                                                   &m_ycbcrInfo, 0);
            if (m_fovDecodeParams)
                cmdList->SetGraphicsRootConstantBufferView(RootParamIndex::FoveatedDecodeParams, swapchainContext.GetFoveationParamCBuffer()->GetGPUVirtualAddress());
            
            cmdList->ClearRenderTargetView(renderTargetView, ALXR::VideoClearColors[ClearColorIndex(newMode)], 0, nullptr);
            cmdList->ClearDepthStencilView(depthStencilView, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            const D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { renderTargetView };
            cmdList->OMSetRenderTargets((UINT)std::size(renderTargets), renderTargets, true, &depthStencilView);

            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // Draw Video Quad
            cmdList->DrawInstanced(3, 1, 0, 0);
        }, RenderPipelineType::Video, newMode);
    }

    virtual void RenderVideoView(
        const std::array<XrCompositionLayerProjectionView, 2>& layerViews,
        const std::array<const XrSwapchainImageBaseHeader*, 2>& swapchainImages,
        const std::int64_t swapchainFormat, const PassthroughMode newMode /*= PassthroughMode::None*/
    ) override
    {
        using CpuDescHandle = D3D12_CPU_DESCRIPTOR_HANDLE;
        using CommandListPtr = ComPtr<ID3D12GraphicsCommandList>;
        RenderViewImpl(layerViews, swapchainImages, swapchainFormat, [&]
        (
            const std::uint32_t viewID,
            const XrCompositionLayerProjectionView& layerView,
            const CommandListPtr& cmdList,
            const CpuDescHandle& renderTargetView,
            const CpuDescHandle& depthStencilView,
            SwapchainImageContext& swapchainContext
        ) {
            CHECK(layerView.subImage.imageArrayIndex == 0);

            if (currentTextureIdx == std::size_t(-1))
                return;
            const auto& videoTex = m_videoTextures[currentTextureIdx];

            ID3D12DescriptorHeap* const ppHeaps[] = { m_srvHeap.Get() };
            cmdList->SetDescriptorHeaps((UINT)std::size(ppHeaps), ppHeaps);
            cmdList->SetGraphicsRootDescriptorTable(RootParamIndex::LumaTexture, videoTex.lumaGpuHandle); // Second texture will be (texture1+1)
            cmdList->SetGraphicsRootDescriptorTable(RootParamIndex::ChromaTexture, videoTex.chromaGpuHandle);
            if (m_is3PlaneFormat)
                cmdList->SetGraphicsRootDescriptorTable(RootParamIndex::ChromaVTexture, videoTex.chromaVGpuHandle);
            cmdList->SetGraphicsRoot32BitConstants(RootParamIndex::YcbcrInfoParams, sizeof(m_ycbcrInfo) / sizeof(std::uint32_t),
                &m_ycbcrInfo, 0);
            if (m_fovDecodeParams)
                cmdList->SetGraphicsRootConstantBufferView(RootParamIndex::FoveatedDecodeParams, swapchainContext.GetFoveationParamCBuffer()->GetGPUVirtualAddress());

            // Set shaders and constant buffers.
            ID3D12Resource* const viewProjectionCBuffer = swapchainContext.GetViewProjectionCBuffer();
            {
                const ALXR::ViewProjectionConstantBuffer viewProjection{ .ViewID = viewID };
                constexpr const D3D12_RANGE NoReadRange{ 0, 0 };
                void* data = nullptr;
                CHECK_HRCMD(viewProjectionCBuffer->Map(0, &NoReadRange, &data));
                assert(data != nullptr);
                std::memcpy(data, &viewProjection, sizeof(viewProjection));
                viewProjectionCBuffer->Unmap(0, nullptr);
            }
            cmdList->SetGraphicsRootConstantBufferView(RootParamIndex::ViewProjTransform, viewProjectionCBuffer->GetGPUVirtualAddress());

            // Clear swapchain and depth buffer. NOTE: This will clear the entire render target view, not just the specified view.
            cmdList->ClearRenderTargetView(renderTargetView, ALXR::VideoClearColors[ClearColorIndex(newMode)], 0, nullptr);
            cmdList->ClearDepthStencilView(depthStencilView, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            const D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { renderTargetView };
            cmdList->OMSetRenderTargets((UINT)std::size(renderTargets), renderTargets, true, &depthStencilView);

            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // Draw Video Quad
            cmdList->DrawInstanced(3, 1, 0, 0);
        }, RenderPipelineType::Video, newMode);
    }

    virtual inline void SetEnvironmentBlendMode(const XrEnvironmentBlendMode newMode) override {
        static_assert(XR_ENVIRONMENT_BLEND_MODE_OPAQUE == 1);
        static_assert(ALXR::ClearColors.size() >= 3);
        static_assert(ALXR::VideoClearColors.size() >= 3);
        assert(newMode > 0 && newMode < 4);
        m_clearColorIndex = (newMode - 1);
    }

    virtual void SetFoveatedDecode(const ALXR::FoveatedDecodeParams* newFovDecParm) override {
        const auto fovDecodeParams = m_fovDecodeParams;
        const bool changePipelines = (fovDecodeParams == nullptr && newFovDecParm != nullptr) ||
                                     (fovDecodeParams != nullptr && newFovDecParm == nullptr);        
        if (changePipelines) {
            m_VideoPipelineStates.clear();
        }
        if (newFovDecParm) {
            for (auto swapchainCtx : m_swapchainImageContexts)
                swapchainCtx->SetFoveationDecodeData(*newFovDecParm);
        }
        m_fovDecodeParams = newFovDecParm ?
            std::make_shared<ALXR::FoveatedDecodeParams>(*newFovDecParm) : nullptr;
    }

    virtual bool SetVisibilityMask(uint32_t viewIndex, const struct XrVisibilityMaskKHR& visibilityMask) override {

        if (!m_enableVisibilityMask ||
            visibilityMask.vertices == nullptr ||
            visibilityMask.indices == nullptr ||
            visibilityMask.indexCountOutput == 0 ||
            visibilityMask.vertexCountOutput == 0 ||
            m_swapchainImageContexts.empty() ||
            m_device == nullptr) {
            return false;
        }

        const auto swapchainImageCtx = m_swapchainImageContexts[0];
        if (swapchainImageCtx == nullptr) {
            Log::Write(Log::Level::Error, "Failed to set visibility mask, swapchainImageContext is not initialized.");
            return false;
        }

        if (m_visibilityMaskState.rtvHeap == nullptr) {
            constexpr const D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                .NumDescriptors = 2,
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
            };
            if (FAILED(m_device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
                reinterpret_cast<void**>(m_visibilityMaskState.rtvHeap.ReleaseAndGetAddressOf())))) {
                Log::Write(Log::Level::Error, "Failed to set visibility mask, could not create rtv-heap");
                return false;
            }
        }

        if (m_visibilityMaskState.dsvHeap == nullptr) {
            constexpr const D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                .NumDescriptors = 2,
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
            };
            if (FAILED(m_device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
                reinterpret_cast<void**>(m_visibilityMaskState.dsvHeap.ReleaseAndGetAddressOf())))) {
                Log::Write(Log::Level::Error, "Failed to set visibility mask, could not create dsv-heap");
                return false;
            }
        }

        if (m_visibilityMaskState.projectionCBuffer == NULL) {
            constexpr const std::uint32_t projPerViewSize = AlignTo<D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>(sizeof(DirectX::XMFLOAT4X4A));
            m_visibilityMaskState.projectionCBuffer = CreateBuffer(m_device.Get(), projPerViewSize * 2, D3D12_HEAP_TYPE_UPLOAD);
            assert(m_visibilityMaskState.projectionCBuffer != nullptr);
            m_visibilityMaskState.projectionCBuffer->SetName(L"VisibilityMask_ProjectionCBuffer");
        }

        if (m_visibilityMaskState.pipelineState == nullptr) {
            constexpr static const std::array<const D3D12_INPUT_ELEMENT_DESC, 1> inputElementDescs = {
                D3D12_INPUT_ELEMENT_DESC {
                    "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
                },
            };
            auto& vmPipelineState = m_visibilityMaskState.pipelineState;
            const DXGI_FORMAT swapchainFormat = swapchainImageCtx->color_format;
            const auto visiblityShaders = m_coreShaders.GetVisibilityMaskCodes();
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc{ .pRootSignature = m_rootSignature.Get() };
            MakeDefaultPipelineStateDesc(pipelineStateDesc, swapchainFormat, visiblityShaders, inputElementDescs, true);
            
            auto& rasterizerState = pipelineStateDesc.RasterizerState;
            rasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            rasterizerState.FrontCounterClockwise = TRUE;

            constexpr const D3D12_DEPTH_STENCILOP_DESC stencilOpDesc = {
                .StencilFailOp = D3D12_STENCIL_OP_KEEP,      // Replace stencil value if stencil test fails,
                .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP, // Replace stencil value if depth test fails (depth test disabled)
                .StencilPassOp = D3D12_STENCIL_OP_REPLACE,   // Replace stencil value if stencil test passes
                .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS, // Always pass stencil test (fill buffer)
            };
            constexpr const D3D12_DEPTH_STENCIL_DESC stencilDesc = {
                .DepthEnable = FALSE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
                .DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS,
                .StencilEnable = TRUE,
                .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,  // Allow reading from all stencil bit
                .StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK, // Allow writing to all stencil bit
                .FrontFace = stencilOpDesc,
                .BackFace = stencilOpDesc,
            };
            pipelineStateDesc.DepthStencilState = stencilDesc;
            if (FAILED(m_device->CreateGraphicsPipelineState(&pipelineStateDesc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void**>(vmPipelineState.ReleaseAndGetAddressOf())))) {
                Log::Write(Log::Level::Error, "Failed to set visibility mask, could not create pipeline-state");
                return false;
            }
        }

        ComPtr<ID3D12GraphicsCommandList> cmdList{};
        if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void**>(cmdList.ReleaseAndGetAddressOf())))) {
            Log::Write(Log::Level::Error, "Failed to set visibility mask, could not create command-list");
            return false;
        }

        auto& vbuff = m_visibilityMaskState.vertexBuffers[viewIndex];

        ComPtr<ID3D12Resource> vertexBufferUpload{};
        vbuff.vertexCount = 0;
        const std::uint32_t vertexBufferSize = visibilityMask.vertexCountOutput * sizeof(XrVector2f);
        if (vbuff.vb = CreateBuffer(m_device.Get(), vertexBufferSize, D3D12_HEAP_TYPE_DEFAULT))
        {
            vertexBufferUpload = CreateBuffer(m_device.Get(), vertexBufferSize, D3D12_HEAP_TYPE_UPLOAD);
            void* data = nullptr;
            const D3D12_RANGE readRange{ 0, 0 };
            if (FAILED(vertexBufferUpload->Map(0, &readRange, &data))) {
                Log::Write(Log::Level::Error, "Failed to set visibility mask, could not map vertex buffer to host");
                return false;
            }
            memcpy(data, visibilityMask.vertices, vertexBufferSize);
            vertexBufferUpload->Unmap(0, nullptr);
            cmdList->CopyBufferRegion(vbuff.vb.Get(), 0, vertexBufferUpload.Get(), 0, vertexBufferSize);
            vbuff.vertexCount = visibilityMask.vertexCountOutput;
        } else return false;

        ComPtr<ID3D12Resource> indexBufferUpload{};
        vbuff.indexCount = 0;
        const std::uint32_t indexBufferSize = visibilityMask.indexCountOutput * sizeof(std::uint32_t);
        if (vbuff.ib = CreateBuffer(m_device.Get(), indexBufferSize, D3D12_HEAP_TYPE_DEFAULT))
        {
            indexBufferUpload = CreateBuffer(m_device.Get(), indexBufferSize, D3D12_HEAP_TYPE_UPLOAD);
            void* data = nullptr;
            const D3D12_RANGE readRange{ 0, 0 };
            if (FAILED(indexBufferUpload->Map(0, &readRange, &data))) {
                Log::Write(Log::Level::Error, "Failed to set visibility mask, could not map index buffer to host");
                return false;
            }
            memcpy(data, visibilityMask.indices, indexBufferSize);
            indexBufferUpload->Unmap(0, nullptr);
            cmdList->CopyBufferRegion(vbuff.ib.Get(), 0, indexBufferUpload.Get(), 0, indexBufferSize);
            vbuff.indexCount = visibilityMask.indexCountOutput;
        } else return false;

        if (FAILED(cmdList->Close())) {
            Log::Write(Log::Level::Error, "Failed to set visibility mask, could not close resource create command-list");
            return false;
        }
        ID3D12CommandList* cmdLists[] = { cmdList.Get() };
        m_cmdQueue->ExecuteCommandLists((UINT)std::size(cmdLists), cmdLists);

        WaitForGpu();

        return m_visibilityMaskState.isDirty = true;
    }

    virtual inline bool IsMultiViewEnabled() const override {
        return m_isMultiViewSupported;
    }

    using ID3D12CommandQueuePtr = ComPtr<ID3D12CommandQueue>;

#include "cuda/d3d12cuda_interop.inl"

   private:
    CoreShaders m_coreShaders{};
    ComPtr<ID3D12Device> m_device;
    LUID             m_dx12deviceluid{};
    ComPtr<ID3D12CommandQueue> m_cmdQueue;
    ComPtr<ID3D12Fence> m_fence;
    std::atomic_uint64_t m_frameFenceValue{ 0 };
    HANDLE m_fenceEvent = INVALID_HANDLE_VALUE;
    
    using SwapchainImageContextPtr = std::shared_ptr<SwapchainImageContext>;
    using SwapchainImageContextWeakPtr = std::weak_ptr<SwapchainImageContext>;
    std::vector<SwapchainImageContextPtr> m_swapchainImageContexts;
    
    using SwapchainImageContextMap = std::unordered_map<const XrSwapchainImageBaseHeader*, SwapchainImageContextWeakPtr>;
    SwapchainImageContextMap m_swapchainImageContextMap;

    XrGraphicsBindingD3D12KHR m_graphicsBinding{
        .type = XR_TYPE_GRAPHICS_BINDING_D3D12_KHR,
        .next = nullptr
    };
    ComPtr<ID3D12RootSignature> m_rootSignature;
    std::map<DXGI_FORMAT, ComPtr<ID3D12PipelineState>> m_pipelineStates;
    ComPtr<ID3D12Resource> m_cubeVertexBuffer;
    ComPtr<ID3D12Resource> m_cubeIndexBuffer;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator{};
    
    static_assert(XR_ENVIRONMENT_BLEND_MODE_OPAQUE == 1);
    std::size_t m_clearColorIndex{ (XR_ENVIRONMENT_BLEND_MODE_OPAQUE - 1) };
    struct VisibilityMaskData final {
        ComPtr<ID3D12DescriptorHeap> rtvHeap{};
        ComPtr<ID3D12DescriptorHeap> dsvHeap{};
        ComPtr<ID3D12Resource> projectionCBuffer{};
        struct VertexBuffer final {
            ComPtr<ID3D12Resource> vb{};
            ComPtr<ID3D12Resource> ib{};
            uint32_t vertexCount{ 0 };
            uint32_t indexCount{ 0 };
        };
        std::array<VertexBuffer, 2> vertexBuffers{};
        ComPtr<ID3D12PipelineState> pipelineState{};
        std::atomic_bool isDirty{ false };
        bool IsValid() const {
            if (pipelineState == nullptr)
                return false;
            for (size_t idx = 0; idx < vertexBuffers.size(); ++idx) {
                if (vertexBuffers[idx].ib == nullptr)
                    return false;
            }
            return true;
        }
    } m_visibilityMaskState;
    ////////////////////////////////////////////////////////////////////////

    D3D12FenceEvent                 m_texRendereComplete {};
    D3D12FenceEvent                 m_texCopy {};
    constexpr static const std::size_t VideoTexCount = 2;
    struct NV12Texture {

        CD3DX12_CPU_DESCRIPTOR_HANDLE lumaHandle{};
        CD3DX12_CPU_DESCRIPTOR_HANDLE chromaHandle{};
        CD3DX12_CPU_DESCRIPTOR_HANDLE chromaVHandle{};

        CD3DX12_GPU_DESCRIPTOR_HANDLE lumaGpuHandle{};
        CD3DX12_GPU_DESCRIPTOR_HANDLE chromaGpuHandle{};
        CD3DX12_GPU_DESCRIPTOR_HANDLE chromaVGpuHandle{};

        // NV12
        ComPtr<ID3D12Resource>  texture{};
        ComPtr<ID3D12Resource>  uploadTexture{};
        ComPtr<ID3D11Texture2D> wrappedD3D11Texture{};
        HANDLE                  wrappedD3D11SharedHandle = INVALID_HANDLE_VALUE;
        HANDLE                  d3d11TextureSharedHandle = INVALID_HANDLE_VALUE;

        // P010LE / CUDA / 3-Plane Fmts
        ComPtr<ID3D12Resource> lumaTexture{};
        ComPtr<ID3D12Resource> chromaTexture{};
        ComPtr<ID3D12Resource> chromaVTexture{};

        ComPtr<ID3D12Resource> lumaStagingBuffer{};
        ComPtr<ID3D12Resource> chromaUStagingBuffer {};
        ComPtr<ID3D12Resource> chromaVStagingBuffer {};

        std::uint64_t          frameIndex = std::uint64_t(-1);
    };
    std::array<NV12Texture, VideoTexCount> m_videoTextures{};
    std::atomic<std::size_t>       m_currentVideoTex{ (std::size_t)0 }, m_renderTex{ (std::size_t)-1 };
    std::atomic<bool>              m_is3PlaneFormat{ false };
    NV12Texture                    m_videoTexUploadBuffers{};
    //std::mutex                   m_renderMutex{};
    ALXR::YcbcrInfoRootConstant    m_ycbcrInfo = {};

    ComPtr<ID3D12CommandAllocator> m_videoTexCmdAllocator{};
    ComPtr<ID3D12CommandQueue>     m_videoTexCmdCpyQueue{};

    using ID3D12PipelineStatePtr = ComPtr<ID3D12PipelineState>;
    using VidePipelineStateList = std::array<ID3D12PipelineStatePtr, VideoPShader::TypeCount>;
    using PipelineStateMap = std::unordered_map<DXGI_FORMAT, VidePipelineStateList>;
    PipelineStateMap m_VideoPipelineStates;

    ComPtr<ID3D12DescriptorHeap> m_srvHeap{};
    ////////////////////////////////////////////////////////////////////////
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    
    using FoveatedDecodeParamsPtr = SwapchainImageContext::FoveatedDecodeParamsPtr;
    FoveatedDecodeParamsPtr m_fovDecodeParams{};

    bool m_isMultiViewSupported = false;
    bool m_enableVisibilityMask = false;
};

}  // namespace

std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_D3D12(const std::shared_ptr<Options>& options,
                                                            std::shared_ptr<IPlatformPlugin> platformPlugin) {
    return std::make_shared<D3D12GraphicsPlugin>(options, platformPlugin);
}

#endif
