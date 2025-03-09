// Copyright (c) 2017-2022, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(XR_USE_GRAPHICS_API_D3D11) || defined(XR_USE_GRAPHICS_API_D3D12)

#include <cstddef>
#include <array>
#include <vector>
#include <string>
#include <filesystem>
#include <DirectXMath.h>
#include "xr_eigen.h"

namespace ALXR {;

struct alignas(16) ModelConstantBuffer {
    DirectX::XMFLOAT4X4A Model;
};
struct alignas(16) ViewProjectionConstantBuffer {
    DirectX::XMFLOAT4X4A ViewProjection;
    std::uint32_t ViewID;
};

struct alignas(16) MultiViewProjectionConstantBuffer {
    DirectX::XMFLOAT4X4A ViewProjection[2];
};

template < std::size_t N >
struct alignas(N) YcbcrInfoConstantBufferT final {
    DirectX::XMFLOAT4X4 dequantizeColorMatrix;
};

using YcbcrInfoConstantBuffer = YcbcrInfoConstantBufferT<16>;
using YcbcrInfoRootConstant = YcbcrInfoConstantBufferT<4>;
static_assert(sizeof(YcbcrInfoRootConstant) <= 256);

enum VideoPShader : std::size_t {
    Normal = 0,
    PassthroughBlend,
    PassthroughMask,
    Normal3Plane,
    PassthroughBlend3Plane,
    PassthroughMask3Plane,
    TypeCount
};

template < typename ShaderByteCodeSpanT >
struct CoreShaders {
    using ShaderByteCodeSpan = ShaderByteCodeSpanT;
    template < const std::size_t N>
    using ShaderByteCodeSpanList = std::array<ShaderByteCodeSpan, N>;

    using ShaderByteCode = std::vector<std::uint8_t>;
    using VideoPShaderList = std::array<ShaderByteCode, VideoPShader::TypeCount>;
    using VideoPShaderMap = std::array<VideoPShaderList, 2>;
    using Path = std::filesystem::path;

    ShaderByteCode  lobbyVS;
    ShaderByteCode  lobbyPS;
    ShaderByteCode  visibilityMaskVS;
    ShaderByteCode  visibilityMaskPS;
    ShaderByteCode  videoVS;
    VideoPShaderMap videoPSMap;

    CoreShaders(const Path& shaderSubDir, const Path& rootPath = Path());
    inline CoreShaders() noexcept = default;
    inline CoreShaders(const CoreShaders&) noexcept = default;
    inline CoreShaders(CoreShaders&&) noexcept = default;
    inline CoreShaders& operator=(const CoreShaders&) noexcept = default;
    inline CoreShaders& operator=(CoreShaders&&) noexcept = default;

    ShaderByteCodeSpanList<2> GetLobbyByteCodes() const {
        return {
            ShaderByteCodeSpan { lobbyVS.data(), lobbyVS.size() },
            ShaderByteCodeSpan { lobbyPS.data(), lobbyPS.size() }
        };
    }

    ShaderByteCodeSpanList<2> GetVisibilityMaskCodes() const {
        return {
            ShaderByteCodeSpan { visibilityMaskVS.data(), visibilityMaskVS.size() },
            ShaderByteCodeSpan { visibilityMaskPS.data(), visibilityMaskPS.size() }
        };
    }

    using VideoByteCodeList = ShaderByteCodeSpanList<VideoPShader::TypeCount + 1>;
    VideoByteCodeList GetVideoByteCodes(const bool useFovDecode) const {
        const auto& videoPSList = videoPSMap[std::size_t(useFovDecode)];
        VideoByteCodeList ret{ ShaderByteCodeSpan { videoVS.data(), videoVS.size() } };
        for (std::size_t index = 0; index < videoPSList.size(); ++index) {
            const auto& ps = videoPSList[index];
            ret[index + 1] = { ps.data(), ps.size() };
        }
        return ret;
    }

    bool IsValid() const {
        for (auto sbcPtr : {
            &lobbyVS,
            &lobbyPS,
            &visibilityMaskVS,
            &visibilityMaskPS,
            &videoVS
        }) {
            if (sbcPtr->empty())
                return false;
        }
        for (const auto& shaderList : videoPSMap) {
            for (const auto& sbc : shaderList) {
                if (sbc.empty())
                    return false;
            }
        }
        return true;
    }
};

using CColorType = DirectX::XMVECTORF32;
    
constexpr inline const std::array<const float, 3> DarkSlateGray { 0.184313729f, 0.309803933f, 0.309803933f };
constexpr inline const std::array<const float, 3> CClear { 0.0f, 0.0f, 0.0f };
    
constexpr inline const std::array<const CColorType, 3> ClearColors { CColorType
    //
    // OpaqueClear - DirectX::Colors::DarkSlateGray
    {DarkSlateGray[0], DarkSlateGray[1], DarkSlateGray[2], 0.2f },
    //
    // AdditiveClear
    { CClear[0], CClear[1], CClear[2], 0.0f },
    //
    // AlphaBlendClear:
    //      set the alpha channel to zero to show passthrough and render elements on top.
    { CClear[0], CClear[1], CClear[2], 0.0f },
};
constexpr inline const std::array<const CColorType, 3> VideoClearColors { CColorType
    //
    // OpaqueClear:
    //      Typically the alpha channel is ignored for XR_ENVIRONMENT_BLEND_MODE_OPAQUE but
    //      for runtimes which only support passthrough via explicit extensions such as XR_FB_passthrough & XR_HTC_passthrough
    //      and only have the an Opaque blend, the alpha channel is relevent/used in this case.
    // 
    { CClear[0], CClear[1], CClear[2], 0.2f },
    // 
    // AdditiveClear
    { CClear[0], CClear[1], CClear[2], 0.0f },
    //
    // AlphaBlendClear:
    //      set the alpha channel to zero to show passthrough, if a video frame isn't rendered for whatever reason
    //      only the passthrough feed will show rather than jarring random black-opaque frames popping in
    //
    { CClear[0], CClear[1], CClear[2], 0.0f },
};

DirectX::XMMATRIX XM_CALLCONV LoadXrPose(const XrPosef& pose);
DirectX::XMMATRIX XM_CALLCONV LoadXrMatrix(const Eigen::Matrix4f& matrix);

std::vector<std::uint8_t> LoadCompiledShaderObject(const std::filesystem::path& csoFile);
Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* hlsl, const char* entrypoint, const char* shaderTarget);
Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromFile(LPCWSTR hlslFile, const D3D_SHADER_MACRO* pDefines, const char* entrypoint, const char* shaderTarget);
Microsoft::WRL::ComPtr<IDXGIAdapter1> GetAdapter(LUID adapterId);

constexpr inline DXGI_FORMAT GetLumaFormat(const DXGI_FORMAT yuvFmt) {
    switch (yuvFmt) {
    case DXGI_FORMAT::DXGI_FORMAT_NV12: return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT::DXGI_FORMAT_P010: return DXGI_FORMAT_R16_UNORM;
    }
    return DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;
}

constexpr inline DXGI_FORMAT GetChromaFormat(const DXGI_FORMAT yuvFmt) {
    switch (yuvFmt) {
    case DXGI_FORMAT::DXGI_FORMAT_NV12: return DXGI_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT::DXGI_FORMAT_P010: return DXGI_FORMAT_R16G16_UNORM;
    }
    return DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;
}

constexpr inline DXGI_FORMAT GetChromaUFormat(const DXGI_FORMAT yuvFmt) {
    switch (yuvFmt) {
    case DXGI_FORMAT::DXGI_FORMAT_R8G8_UNORM: return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT::DXGI_FORMAT_R16G16_UNORM: return DXGI_FORMAT_R16_UNORM;
    }
    return DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;
}

constexpr inline DXGI_FORMAT GetChromaVFormat(const DXGI_FORMAT yuvFmt) {
    switch (yuvFmt) {
    case DXGI_FORMAT::DXGI_FORMAT_R8G8_UNORM: return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT::DXGI_FORMAT_R16G16_UNORM: return DXGI_FORMAT_R16_UNORM;
    }
    return DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;
}

template < typename T >
inline CoreShaders<T>::CoreShaders(
    const typename CoreShaders<T>::Path& shaderSubDir,
    const typename CoreShaders<T>::Path& rootPath /*= Path()*/
)
{
    const auto GetCSOPath = [&shaderSubDir, &rootPath](const Path& csoFile) -> Path
    {
        using namespace std::filesystem;
        std::error_code ec;
        if (exists(csoFile, ec) && !ec)
            return csoFile;

        ec.clear();
        if (!rootPath.empty() &&
            exists(rootPath / csoFile, ec) &&
            !ec) {
            return (rootPath / csoFile);
        }
        std::array<Path, 2> alternateDirs{
            Path{ "shaders" },
#ifdef NDEBUG
            Path{ "target/release/shaders" } // for `cargo run`
#else
            Path{ "target/debug/shaders" } // for `cargo run`
#endif
        };
        for (auto& csoPath : alternateDirs) {
            if (!rootPath.empty()) {
                csoPath = rootPath / csoPath;
            }
            csoPath /= shaderSubDir / csoFile;
            ec.clear();
            if (exists(csoPath, ec) && !ec)
                return csoPath;
        }
        return {};
    };
    const auto LoadCSO = [&GetCSOPath](const Path& csoFile) {
        const auto csoPath = GetCSOPath(csoFile);
        CHECK_MSG(!csoPath.empty(), "CSO path/file does not exist.");
        const auto csoPathStr = csoPath.string();
        Log::Write(Log::Level::Verbose, Fmt("Loading D3D compiled shader object: %s", csoPathStr.c_str()));
        auto cso = LoadCompiledShaderObject(csoPath);
        CHECK_MSG(cso.size() > 0, "Failed to load CSO file!");
        return cso;
    };
    lobbyVS = LoadCSO("lobby_vert.cso");
    lobbyPS = LoadCSO("lobby_frag.cso");
    visibilityMaskVS = LoadCSO("visibilityMask_vert.cso");
    visibilityMaskPS = LoadCSO("visibilityMask_frag.cso");
    videoVS = LoadCSO("videoStream_vert.cso");

    std::size_t psListIndex = 0;
    for (const Path& subdir : { Path {}, Path{"fovDecode"} }) {
        const auto LoadVideoCSO = [&](const Path& csofile) { return LoadCSO(subdir / csofile); };
        assert(psListIndex < videoPSMap.size());
        videoPSMap[psListIndex++] = VideoPShaderList{
            LoadVideoCSO("videoStream_frag.cso"),
            LoadVideoCSO("passthroughBlend_frag.cso"),
            LoadVideoCSO("passthroughMask_frag.cso"),
            LoadVideoCSO("yuv3PlaneFmt/videoStream_frag.cso"),
            LoadVideoCSO("yuv3PlaneFmt/passthroughBlend_frag.cso"),
            LoadVideoCSO("yuv3PlaneFmt/passthroughMask_frag.cso"),
        };
    }
    CHECK(IsValid());
}

}
#endif
