#pragma once
#ifdef XR_ENABLE_CUDA_INTEROP
struct CudaSharedTexture
{
    cudaGraphicsResource_t cudaLumaResource = nullptr;
    cudaGraphicsResource_t cudaChromaResource = nullptr;
    cudaArray_t lumaArray{};
    cudaArray_t chromaArray{};
};

std::array<CudaSharedTexture, 2> m_videoTexturesCuda{};
cudaExternalSemaphore_t m_texCopyExtSemaphore{};
cudaExternalSemaphore_t m_texRenderExtSemaphore{};
cudaStream_t videoBufferStream{};

unsigned int m_nodeMask = 0;
int m_cudaDeviceID = 0;

static void ImportFenceToCuda(const ComPtr<ID3D11Fence>& fence, cudaExternalSemaphore_t& externalSemphore)
{
    HANDLE sharedHandle{};
    const WindowsSecurityAttributes windowsSecurityAttributes{};
    fence->CreateSharedHandle(&windowsSecurityAttributes, GENERIC_ALL, /*name=*/nullptr, &sharedHandle);

    const cudaExternalSemaphoreHandleDesc externalSemaphoreHandleDesc{
        .type = cudaExternalSemaphoreHandleTypeD3D11Fence,
        .handle{.win32{.handle = sharedHandle}},
        .flags = 0
    };
    /*CheckCudaErrors*/
    if (cudaImportExternalSemaphore(&externalSemphore, &externalSemaphoreHandleDesc) != cudaSuccess)
    {
        Log::Write(Log::Level::Error, "Failed to cudaCreateSurfaceObject.");
        CHECK(false);
    }
    CloseHandle(sharedHandle);
};

void InitCuda()
{
    int cudaDeviceCount = 0;
    /*CheckCudaErrors*/cudaGetDeviceCount(&cudaDeviceCount);
    if (cudaDeviceCount == 0)
        Log::Write(Log::Level::Error, "No cuda devices found.");

    for (int devId = 0; devId < cudaDeviceCount; devId++) {
        cudaDeviceProp devProp;
        /*checkCudaErrors*/cudaGetDeviceProperties(&devProp, devId);

        if ((memcmp(&m_d3d11DeviceLUID.LowPart, devProp.luid,
            sizeof(m_d3d11DeviceLUID.LowPart)) == 0) &&
            (memcmp(&m_d3d11DeviceLUID.HighPart,
                devProp.luid + sizeof(m_d3d11DeviceLUID.LowPart),
                sizeof(m_d3d11DeviceLUID.HighPart)) == 0)) {
            /*checkCudaErrors*/cudaSetDevice(devId);
            m_cudaDeviceID = devId;
            m_nodeMask = devProp.luidDeviceNodeMask;
            cudaStreamCreateWithFlags(&videoBufferStream, cudaStreamNonBlocking);
            Log::Write(Log::Level::Info, Fmt("CUDA Device Used [%d] %s\n", devId, devProp.name));

            ImportFenceToCuda(m_texCopy.fence, m_texCopyExtSemaphore);
            ImportFenceToCuda(m_texRendereComplete.fence, m_texRenderExtSemaphore);

            return;
        }
    }
    CHECK(false);
}

static constexpr inline cudaChannelFormatDesc CreateChannelDesc(const DXGI_FORMAT fmt)
{
    constexpr const std::size_t N = sizeof(std::uint16_t) * 8;
    switch (fmt) {
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_UNORM:
        return cudaCreateChannelDesc<std::uint8_t>();
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_UNORM:
        return cudaCreateChannelDesc<std::uint16_t>();
    case DXGI_FORMAT_R8G8_UINT:
        return cudaCreateChannelDesc(8, 8, 0, 0, cudaChannelFormatKindUnsigned);
    case DXGI_FORMAT_R8G8_UNORM:// return cudaCreateChannelDesc<std::uint16_t>();
        return cudaCreateChannelDesc(8, 8, 0, 0, cudaChannelFormatKindUnsignedNormalized8X2);
    case DXGI_FORMAT_R16G16_UINT:
        return cudaCreateChannelDesc(16, 16, 0, 0, cudaChannelFormatKindUnsigned);
    case DXGI_FORMAT_R16G16_UNORM:
        return cudaCreateChannelDesc(16, 16, 0, 0, cudaChannelFormatKindUnsignedNormalized16X2);
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
        return cudaCreateChannelDesc(16, 32, 0, 0, cudaChannelFormatKindNone);//cudaChannelFormatKindNV12);
    case DXGI_FORMAT_NV12:
        return cudaCreateChannelDescNV12();
    }
    return cudaCreateChannelDesc(0, 0, 0, 0, cudaChannelFormatKindNone);
}

inline void ClearCudaVideoTextures()
{
    m_videoTexturesCuda = {};
    ClearVideoTextures();
}

virtual void CreateVideoTexturesCUDA(const CreateVideoTextureInfo& info) override
{
    if (m_device == nullptr)
        return;

    CHECK_MSG((info.pixfmt != ALXR::YcbcrFormat::G8_B8_R8_3PLANE_420 &&
        info.pixfmt != ALXR::YcbcrFormat::G10X6_B10X6_R10X6_3PLANE_420), "3-Planes formats are not supported!");
    
    ClearCudaVideoTextures();

    m_ycbcrInfo = MakeYcbcrInfo(info);

    const auto yuvFormat = MapFormat(info.pixfmt);

    /*constexpr*/ const DXGI_FORMAT LUMA_FORMAT = ALXR::GetLumaFormat(yuvFormat);
    /*constexpr*/ const DXGI_FORMAT CHROMA_FORMAT = ALXR::GetChromaFormat(yuvFormat);

    size_t texIndex = 0;
    for (std::size_t index = 0; index < m_videoTextures.size(); ++index)
    {
        auto& videoTex = m_videoTextures[index];

        const D3D11_TEXTURE2D_DESC lumaTextureDesc {
            .Width = static_cast<UINT> (info.width),
            .Height = static_cast<UINT>(info.height),
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = LUMA_FORMAT,
            .SampleDesc {
                .Count = 1,
                .Quality = 0
            },
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = 0,
            .MiscFlags = 0,// D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE,
        };
        ComPtr<ID3D11Texture2D> newLumaTexture{};
        CHECK_HRCMD(m_device->CreateTexture2D(&lumaTextureDesc, nullptr, newLumaTexture.ReleaseAndGetAddressOf()));
        CHECK(newLumaTexture != nullptr);
        videoTex.data.push_back(newLumaTexture);

        D3D11_TEXTURE2D_DESC chromaTextureDesc = lumaTextureDesc;
        chromaTextureDesc.Format = CHROMA_FORMAT;
        chromaTextureDesc.Width = chromaTextureDesc.Width / 2;
        chromaTextureDesc.Height = chromaTextureDesc.Height / 2;
        
        ComPtr<ID3D11Texture2D> newChromaTexture{};
        CHECK_HRCMD(m_device->CreateTexture2D(&chromaTextureDesc, nullptr, newChromaTexture.ReleaseAndGetAddressOf()));
        videoTex.data.push_back(newChromaTexture);

        const auto lumaPlaneSRVDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC
        (
            newLumaTexture.Get(),
            D3D11_SRV_DIMENSION_TEXTURE2D,
            LUMA_FORMAT
        );
        CHECK_HRCMD(m_device->CreateShaderResourceView(newLumaTexture.Get(), &lumaPlaneSRVDesc, videoTex.lumaSRV.ReleaseAndGetAddressOf()));

        const auto chromaPlaneSRVDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC
        (
            newChromaTexture.Get(),
            D3D11_SRV_DIMENSION_TEXTURE2D,
            CHROMA_FORMAT
        );
        CHECK_HRCMD(m_device->CreateShaderResourceView(newChromaTexture.Get(), &chromaPlaneSRVDesc, videoTex.chromaSRV.ReleaseAndGetAddressOf()));
        
        const auto cudaImportD3D11Texture = [](const ComPtr<ID3D11Texture2D>& texture, cudaGraphicsResource_t& cudaResource, cudaArray_t& texArray)
        {
            cudaError_t error = cudaGraphicsD3D11RegisterResource(&cudaResource, texture.Get(), cudaGraphicsRegisterFlagsNone);
            if (error != cudaSuccess)
            {
                Log::Write(Log::Level::Error, Fmt("Failed to cudaGraphicsD3D11RegisterResource. %d", error));
                CHECK(false);
            }
            cudaGraphicsResourceSetMapFlags(cudaResource, cudaGraphicsMapFlagsWriteDiscard);

            error = cudaGraphicsMapResources(1, &cudaResource);
            if (error != cudaSuccess)
            {
                Log::Write(Log::Level::Error, Fmt("Failed to cudaGraphicsMapResources. %d", error));
                CHECK(false);
            }

            cudaMipmappedArray_t cuMiArray{};
            error = cudaGraphicsResourceGetMappedMipmappedArray(&cuMiArray, cudaResource);
            if (error != cudaSuccess)
            {
                Log::Write(Log::Level::Error, Fmt("Failed to cudaGraphicsResourceGetMappedMipmappedArray. %d", error));
                CHECK(false);
            }

            if (cudaGetMipmappedArrayLevel(&texArray, cuMiArray, 0) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, Fmt("Failed to cudaGetMipmappedArrayLevel. %d", error));
                CHECK(false);
            }
            CHECK(texArray);
        };

        auto& newSharedTex = m_videoTexturesCuda[index];
        cudaImportD3D11Texture(newLumaTexture, newSharedTex.cudaLumaResource, newSharedTex.lumaArray);
        cudaImportD3D11Texture(newChromaTexture, newSharedTex.cudaChromaResource, newSharedTex.chromaArray);

        // Create synchronization objects and wait until assets have been uploaded to the GPU.

        ComPtr<ID3D11Device5> dev5;
        CHECK_HRCMD(m_device.As(&dev5));
        CHECK(dev5 != nullptr);

        cudaExternalSemaphore_t createTexSemp{};
        D3D11FenceEvent createTexEvent{};
        createTexEvent.CreateFence(dev5, D3D11_FENCE_FLAG_SHARED);
        ImportFenceToCuda(createTexEvent.fence, createTexSemp);
        createTexEvent.WaitForGpu();
    }
}

virtual void UpdateVideoTextureCUDA(const YUVBuffer& yuvBuffer) override
{
    CHECK(m_device != nullptr);

    const cudaExternalSemaphoreWaitParams externalSemaphoreWaitParams{
        .params{.fence{.value = m_texRendereComplete.fenceValue.load()}},
        .flags = 0,
    };
    if (cudaWaitExternalSemaphoresAsync(&m_texRenderExtSemaphore, &externalSemaphoreWaitParams, 1, videoBufferStream) != cudaSuccess)
    {
        Log::Write(Log::Level::Error, "cudaWaitExternalSemaphoresAsync failed.");
        CHECK(false);
    }

    const std::size_t freeIndex = m_currentVideoTex.load();
    const auto& sharedTex = m_videoTexturesCuda[freeIndex];
    auto& videoTex = m_videoTextures[freeIndex];
    videoTex.frameIndex = yuvBuffer.frameIndex;
    CHECK(videoTex.data.size() == 2);
    D3D11_TEXTURE2D_DESC lumaDesc {}, chromaDesc{};
    {
        auto lumaTex = videoTex.data[0];
        auto chromaTex = videoTex.data[1];
        lumaTex->GetDesc(&lumaDesc);
        chromaTex->GetDesc(&chromaDesc);
    }

    if (cudaMemcpy2DToArrayAsync
    (
        sharedTex.lumaArray, 0, 0,
        yuvBuffer.luma.data, yuvBuffer.luma.pitch, lumaDesc.Width * SizeOfFmt(lumaDesc.Format), yuvBuffer.luma.height,
        cudaMemcpyDeviceToDevice//,
        //videoBufferStream
    ) != cudaSuccess) {
        Log::Write(Log::Level::Error, "Failed to cudaMemcpy2DToArray.");
        CHECK(false);
    }

    if (cudaMemcpy2DToArrayAsync
    (
        sharedTex.chromaArray, 0, 0,
        yuvBuffer.chroma.data, yuvBuffer.chroma.pitch, chromaDesc.Width * SizeOfFmt(chromaDesc.Format), yuvBuffer.chroma.height,
        cudaMemcpyDeviceToDevice//,
        //videoBufferStream
    ) != cudaSuccess) {
        Log::Write(Log::Level::Error, "Failed to cudaMemcpy2DToArray.");
        CHECK(false);
    }
    //cudaStreamSynchronize(videoBufferStream);

    m_currentVideoTex.store((freeIndex + 1) % VideoTexCount);

    const auto nextVal = m_texCopy.fenceValue.load() + 1;
    const cudaExternalSemaphoreSignalParams externalSemaphoreSignalParams{
        .params{.fence{.value = nextVal}},
        .flags = 0
    };
    if (cudaSignalExternalSemaphoresAsync(&m_texCopyExtSemaphore, &externalSemaphoreSignalParams, 1, videoBufferStream) != cudaSuccess)
    {
        Log::Write(Log::Level::Error, "m_texCopy cudaSignalExternalSemaphoresAsync failed.");
        CHECK(false);
    }
    //CHECK_HRCMD(m_texCopy.Signal(m_videoTexCmdCpyQueue));
    m_texCopy.fenceValue.store(nextVal);
    m_renderTex.store(freeIndex);
}
#endif
