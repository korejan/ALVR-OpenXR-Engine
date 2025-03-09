#pragma once
#ifdef XR_ENABLE_CUDA_INTEROP
struct CudaSharedTexture
{
    static_assert(sizeof(std::uintptr_t) == sizeof(cudaSurfaceObject_t));

    cudaExternalMemory_t m_externalMemory{};
    cudaExternalMemory_t m_externalMemoryChroma{};
        
    cudaArray_t lumaArray{};    
    cudaArray_t chromaArray{};
};

ComPtr<ID3D12CommandAllocator> cudaCommandAllocator{};
std::array<CudaSharedTexture, 2> m_videoTexturesCuda{};

cudaStream_t videoBufferStream{};
cudaExternalSemaphore_t m_texCopyExtSemaphore{};
cudaExternalSemaphore_t m_texRenderExtSemaphore{};

unsigned int m_nodeMask = 0;
int m_cudaDeviceID = 0;

static inline void SetNameIndexed(ID3D12Object* pObject, LPCWSTR name, std::size_t index) {
    WCHAR fullName[50];
    if (swprintf_s(fullName, L"%s[%zu]", name, index) > 0) {
        pObject->SetName(fullName);
    }
}

inline void ImportFenceToCuda(const ComPtr<ID3D12Fence>& fence, auto& externalSemphore)
{
    HANDLE sharedHandle{};
    const WindowsSecurityAttributes windowsSecurityAttributes{};
    m_device->CreateSharedHandle(fence.Get(), &windowsSecurityAttributes, GENERIC_ALL, /*name=*/nullptr, &sharedHandle);

    const cudaExternalSemaphoreHandleDesc externalSemaphoreHandleDesc {
        .type = cudaExternalSemaphoreHandleTypeD3D12Fence,
        .handle { .win32 { .handle = sharedHandle } },
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

        if ((memcmp(&m_dx12deviceluid.LowPart, devProp.luid,
            sizeof(m_dx12deviceluid.LowPart)) == 0) &&
            (memcmp(&m_dx12deviceluid.HighPart,
                devProp.luid + sizeof(m_dx12deviceluid.LowPart),
                sizeof(m_dx12deviceluid.HighPart)) == 0)) {
            /*checkCudaErrors*/cudaSetDevice(devId);
            m_cudaDeviceID = devId;
            m_nodeMask = devProp.luidDeviceNodeMask;
            cudaStreamCreateWithFlags(&videoBufferStream, cudaStreamNonBlocking);
            Log::Write(Log::Level::Info, Fmt("CUDA Device Used [%d] %s\n", devId, devProp.name));

            CHECK_HRCMD(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void**>(cudaCommandAllocator.ReleaseAndGetAddressOf())));

            ImportFenceToCuda(m_texCopy.fence, m_texCopyExtSemaphore);
            ImportFenceToCuda(m_texRendereComplete.fence, m_texRenderExtSemaphore);

            return;
        }
    }
    CHECK(false);
}

static constexpr inline std::size_t SizeOfFmt(const DXGI_FORMAT fmt)
{
    switch (fmt) {
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_UNORM:      return sizeof(std::uint8_t);
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_UNORM:    return sizeof(std::uint8_t) * 2;
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_UNORM:     return sizeof(std::uint16_t);
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_UNORM:  return sizeof(std::uint16_t) * 2;
    }
    return 0;
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

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    CHECK_HRCMD(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cudaCommandAllocator.Get(), nullptr,
        __uuidof(ID3D12GraphicsCommandList),
        reinterpret_cast<void**>(cmdList.ReleaseAndGetAddressOf())));

    const auto yuvFormat = MapFormat(info.pixfmt);

    /*constexpr*/ const DXGI_FORMAT LUMA_FORMAT = ALXR::GetLumaFormat(yuvFormat);
    /*constexpr*/ const DXGI_FORMAT CHROMA_FORMAT = ALXR::GetChromaFormat(yuvFormat);

    const std::uint32_t descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    size_t texIndex = 0;
    for (std::size_t index = 0; index < m_videoTextures.size(); ++index)
    {
        auto& videoTex = m_videoTextures[index];

        const D3D12_HEAP_PROPERTIES heapProp {
            .Type = D3D12_HEAP_TYPE_DEFAULT,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN
        };
        const D3D12_RESOURCE_DESC lumaTextureDesc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            .Alignment = 0,
            .Width = static_cast<UINT>(info.width),
            .Height = static_cast<UINT>(info.height),
            .DepthOrArraySize = 1,
            .MipLevels = 1,
            .Format = LUMA_FORMAT,
            .SampleDesc {
                .Count = 1,
                .Quality = 0
            },
            .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            .Flags = D3D12_RESOURCE_FLAG_NONE
        };
        ComPtr<ID3D12Resource> newLumaTexture;
        CHECK_HRCMD(m_device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_SHARED,
            &lumaTextureDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,
            __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(newLumaTexture.ReleaseAndGetAddressOf())));
        SetNameIndexed(newLumaTexture.Get(), L"CudaSharedTexture", texIndex++);
        videoTex.lumaTexture = newLumaTexture;

        D3D12_RESOURCE_DESC chromaTextureDesc = lumaTextureDesc;
        chromaTextureDesc.Format = CHROMA_FORMAT;
        chromaTextureDesc.Width = chromaTextureDesc.Width / 2;
        chromaTextureDesc.Height = chromaTextureDesc.Height / 2;

        ComPtr<ID3D12Resource> newChromaTexture;
        CHECK_HRCMD(m_device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_SHARED,
            &chromaTextureDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,
            __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(newChromaTexture.ReleaseAndGetAddressOf())));
        SetNameIndexed(newChromaTexture.Get(), L"CudaSharedTexture", texIndex++);
        videoTex.chromaTexture = newChromaTexture;

        //////////////
        // Describe and create a SRV for the texture.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
            .Format = LUMA_FORMAT,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D {.MipLevels = 1},
        };
        m_device->CreateShaderResourceView(videoTex.lumaTexture.Get(), &srvDesc, cpuHandle);
        videoTex.lumaHandle = cpuHandle;
        videoTex.lumaGpuHandle = gpuHandle;
        cpuHandle.Offset(1, descriptorSize);
        gpuHandle.Offset(1, descriptorSize);

        srvDesc.Format = CHROMA_FORMAT;
        m_device->CreateShaderResourceView(videoTex.chromaTexture.Get(), &srvDesc, cpuHandle);
        videoTex.chromaHandle = cpuHandle;
        videoTex.chromaGpuHandle = gpuHandle;
        cpuHandle.Offset(1, descriptorSize);
        gpuHandle.Offset(1, descriptorSize);

        // importation of the D3D12 texture into a cuda surface
        {
            auto& newSharedTex = m_videoTexturesCuda[index];
            HANDLE sharedLumaHandle{};
            HANDLE sharedChromaHandle{};
            const WindowsSecurityAttributes secAttr{};
            CHECK_HRCMD(m_device->CreateSharedHandle(newLumaTexture.Get(), &secAttr, GENERIC_ALL, 0, &sharedLumaHandle));
            const auto lumaTexAllocInfo = m_device->GetResourceAllocationInfo(m_nodeMask, 1, &lumaTextureDesc);

            CHECK_HRCMD(m_device->CreateSharedHandle(newChromaTexture.Get(), &secAttr, GENERIC_ALL, 0, &sharedChromaHandle));
            const auto chromaTexAllocInfo = m_device->GetResourceAllocationInfo(m_nodeMask, 1, &chromaTextureDesc);

            cudaExternalMemoryHandleDesc cuExtmemHandleDesc {
                .type = cudaExternalMemoryHandleTypeD3D12Heap,
                .handle { .win32 { .handle = sharedLumaHandle } },
                .size = lumaTexAllocInfo.SizeInBytes,
                .flags = cudaExternalMemoryDedicated,
            };
            /*CheckCudaErrors*/
            if (cudaImportExternalMemory(&newSharedTex.m_externalMemory, &cuExtmemHandleDesc) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, "Failed to import external memory to cuda.");
                CHECK(false);
            }
            CloseHandle(sharedLumaHandle);

            cuExtmemHandleDesc = {
                .type = cudaExternalMemoryHandleTypeD3D12Heap,
                .handle { .win32 { .handle = sharedChromaHandle } },
                .size = chromaTexAllocInfo.SizeInBytes,
                .flags = cudaExternalMemoryDedicated,
            };
            /*CheckCudaErrors*/
            if (cudaImportExternalMemory(&newSharedTex.m_externalMemoryChroma, &cuExtmemHandleDesc) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, "Failed to import external memory to cuda.");
                CHECK(false);
            }
            CloseHandle(sharedChromaHandle);

            cudaExternalMemoryMipmappedArrayDesc cuExtmemMipDesc{
                .offset = 0,
                .formatDesc = CreateChannelDesc(lumaTextureDesc.Format),
                .extent = make_cudaExtent(lumaTextureDesc.Width, lumaTextureDesc.Height, 0),
                .flags = cudaArrayColorAttachment,
                .numLevels = 1                
            };
            cudaMipmappedArray_t cuMipArray{};
            /*CheckCudaErrors*/
            if (cudaExternalMemoryGetMappedMipmappedArray(&cuMipArray, newSharedTex.m_externalMemory, &cuExtmemMipDesc) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, "Failed to GetMappedMipmappedArray.");
                CHECK(false);
            }

            /*CheckCudaErrors*/
            if (cudaGetMipmappedArrayLevel(&newSharedTex.lumaArray, cuMipArray, 0) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, "Failed to cudaGetMipmappedArrayLevel.");
                CHECK(false);
            }

            cuExtmemMipDesc = {
                .offset = 0,
                .formatDesc = CreateChannelDesc(chromaTextureDesc.Format),
                .extent = make_cudaExtent(chromaTextureDesc.Width, chromaTextureDesc.Height, 0),
                .flags = cudaArrayColorAttachment,
                .numLevels = 1                
            };
            cuMipArray = {};
            /*CheckCudaErrors*/
            if (cudaExternalMemoryGetMappedMipmappedArray(&cuMipArray, newSharedTex.m_externalMemoryChroma, &cuExtmemMipDesc) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, "Failed to GetMappedMipmappedArray.");
                CHECK(false);
            }

            /*CheckCudaErrors*/
            if (cudaGetMipmappedArrayLevel(&newSharedTex.chromaArray, cuMipArray, 0) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, "Failed to cudaGetMipmappedArrayLevel.");
                CHECK(false);
            }
        }
    }
    CHECK_HRCMD(cmdList->Close());
    ID3D12CommandList* cmdLists[] = { cmdList.Get() };
    m_cmdQueue->ExecuteCommandLists((UINT)ArraySize(cmdLists), cmdLists);

    cudaExternalSemaphore_t createTexSemp{};
    D3D12FenceEvent createTexEvent{};
    createTexEvent.CreateFence(m_device, D3D12_FENCE_FLAG_SHARED);
    ImportFenceToCuda(createTexEvent.fence, createTexSemp);
    createTexEvent.WaitForGpu();
}

virtual void UpdateVideoTextureCUDA(const YUVBuffer& yuvBuffer) override
{
    CHECK(m_device != nullptr);
    CHECK(m_videoTexCmdAllocator != nullptr);
    CHECK(yuvBuffer.frameIndex != std::uint64_t(-1));

    const cudaExternalSemaphoreWaitParams externalSemaphoreWaitParams {
        .params{.fence{.value = m_texRendereComplete.fenceValue.load()}},
        .flags = 0
    };
    if (cudaWaitExternalSemaphoresAsync(&m_texRenderExtSemaphore, &externalSemaphoreWaitParams, 1, videoBufferStream) != cudaSuccess)
    {
        Log::Write(Log::Level::Error, "cudaWaitExternalSemaphoresAsync failed.");
        CHECK(false);
    }

    const std::size_t freeIndex = m_currentVideoTex.load();
    const auto& videoTex = m_videoTexturesCuda[freeIndex];
    {
        auto& newSharedTex = m_videoTextures[freeIndex];
        newSharedTex.frameIndex = yuvBuffer.frameIndex;

        const D3D12_RESOURCE_DESC lumaDesc = newSharedTex.lumaTexture->GetDesc();
        const D3D12_RESOURCE_DESC chromaDesc = newSharedTex.chromaTexture->GetDesc();

        if (cudaMemcpy2DToArrayAsync
        (
            videoTex.lumaArray, 0, 0,
            yuvBuffer.luma.data, yuvBuffer.luma.pitch, lumaDesc.Width * SizeOfFmt(lumaDesc.Format), yuvBuffer.luma.height,
            cudaMemcpyDeviceToDevice//,
            //videoBufferStream
        ) != cudaSuccess) {
            Log::Write(Log::Level::Error, "Failed to cudaMemcpy2DToArray.");
            CHECK(false);
        }

        if (cudaMemcpy2DToArrayAsync
        (
            videoTex.chromaArray, 0, 0,
            yuvBuffer.chroma.data, yuvBuffer.chroma.pitch, chromaDesc.Width * SizeOfFmt(chromaDesc.Format), yuvBuffer.chroma.height,
            cudaMemcpyDeviceToDevice//,
            //videoBufferStream
        ) != cudaSuccess) {
            Log::Write(Log::Level::Error, "Failed to cudaMemcpy2DToArray.");
            CHECK(false);
        }

        //cudaStreamSynchronize(videoBufferStream);
    }

    m_currentVideoTex.store((freeIndex + 1) % VideoTexCount);

    const auto nextVal = m_texCopy.fenceValue.load() + 1;
    const cudaExternalSemaphoreSignalParams externalSemaphoreSignalParams {
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

    //cudaStreamSynchronize(videoBufferStream);
}
#endif
