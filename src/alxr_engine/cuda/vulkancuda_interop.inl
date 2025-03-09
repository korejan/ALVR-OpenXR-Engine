#pragma once

#ifdef _WIN64
PFN_vkGetMemoryWin32HandleKHR fpGetMemoryWin32HandleKHR = nullptr;
PFN_vkGetSemaphoreWin32HandleKHR fpGetSemaphoreWin32HandleKHR = nullptr;
#else
PFN_vkGetMemoryFdKHR fpGetMemoryFdKHR = nullptr;
PFN_vkGetSemaphoreFdKHR fpGetSemaphoreFdKHR = nullptr;
#endif

#ifdef XR_ENABLE_CUDA_INTEROP

struct CudaSharedTexture
{
    static_assert(sizeof(std::uintptr_t) == sizeof(cudaSurfaceObject_t));

    std::array<cudaExternalMemory_t,2> m_externalMemoryList {};
    std::array<cudaArray_t, 2> planeArrays{};
};

std::array<CudaSharedTexture, 2> m_videoTexturesCuda{};

cudaStream_t videoBufferStream = nullptr;//{};
cudaExternalSemaphore_t m_texCopyExtSemaphore{};
cudaExternalSemaphore_t m_texRenderExtSemaphore{};

unsigned int m_nodeMask = 0;
int m_cudaDeviceID = 0;

void InitVKExts()
{
#ifdef _WIN64
    fpGetMemoryWin32HandleKHR =
        (PFN_vkGetMemoryWin32HandleKHR)vkGetInstanceProcAddr(m_vkInstance, "vkGetMemoryWin32HandleKHR");
    if (fpGetMemoryWin32HandleKHR == nullptr) {
        throw std::runtime_error(
            "Vulkan: Proc address for \"vkGetMemoryWin32HandleKHR\" not "
            "found.\n");
    }
#else
    fpGetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)vkGetInstanceProcAddr(
        m_vkInstance, "vkGetMemoryFdKHR");
    if (fpGetMemoryFdKHR == nullptr) {
        throw std::runtime_error(
            "Vulkan: Proc address for \"vkGetMemoryFdKHR\" not found.\n");
    }
    else {
        std::cout << "Vulkan proc address for vkGetMemoryFdKHR - "
            << fpGetMemoryFdKHR << std::endl;
    }
#endif

#ifdef _WIN64
    fpGetSemaphoreWin32HandleKHR =
        (PFN_vkGetSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(m_vkDevice, "vkGetSemaphoreWin32HandleKHR");
    if (fpGetSemaphoreWin32HandleKHR == NULL) {
        throw std::runtime_error(
            "Vulkan: Proc address for \"vkGetSemaphoreWin32HandleKHR\" not "
            "found.\n");
    }
#else
    fpGetSemaphoreFdKHR = (PFN_vkGetSemaphoreFdKHR)vkGetDeviceProcAddr(m_vkDevice, "vkGetSemaphoreFdKHR");
    if (fpGetSemaphoreFdKHR == NULL) {
        throw std::runtime_error(
            "Vulkan: Proc address for \"vkGetSemaphoreFdKHR\" not found.\n");
    }
#endif
}

void ImportSemaphoreToCuda(const VkSemaphore& fence, cudaExternalSemaphore_t& externalSemphore)
{
    const cudaExternalSemaphoreHandleDesc externalSemaphoreHandleDesc{
#ifdef _WIN64
            .type = /*IsWindows8OrGreater()*/ true ?
                cudaExternalSemaphoreHandleTypeOpaqueWin32 : cudaExternalSemaphoreHandleTypeOpaqueWin32Kmt,
            .handle{.win32{.handle = getVkSemaphoreHandle(/*IsWindows8OrGreater()*/ true ?
                                                VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT
                                                : VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT, fence)}},
#else
            .type = cudaExternalSemaphoreHandleTypeOpaqueFd,
            .handle{.fd = getVkSemaphoreHandle(VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT, fence)},
#endif
            .flags = 0
    };
    if (cudaImportExternalSemaphore(&externalSemphore, &externalSemaphoreHandleDesc) != cudaSuccess)
    {
        Log::Write(Log::Level::Error, "Failed to cudaImportExternalSemaphore.");
        CHECK(false);
    }
};

void InitCuda()
{ 
    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess ||
        device_count == 0) {
        Log::Write(Log::Level::Error, "CUDA error: no devices supporting CUDA.\n");
        exit(EXIT_FAILURE);
    }

    int devices_prohibited = 0;    
    // Find the GPU which is selected by Vulkan
    for (int current_device = 0; current_device < device_count; ++current_device)
    {
        cudaDeviceProp deviceProp;
        cudaGetDeviceProperties(&deviceProp, current_device);
        if ((deviceProp.computeMode != cudaComputeModeProhibited))
        {
            // Compare the cuda device UUID with vulkan UUID
            int ret = std::memcmp((void*)&deviceProp.uuid, m_vkDeviceUUID.data(), m_vkDeviceUUID.size());
            if (ret == 0)
            {
                /*checkCudaErrors*/(cudaSetDevice(current_device));
                /*checkCudaErrors*/(cudaGetDeviceProperties(&deviceProp, current_device));
                Log::Write(Log::Level::Info, Fmt("GPU Device %d: \"%s\" with compute capability %d.%d\n\n",
                    current_device, deviceProp.name, deviceProp.major, deviceProp.minor));
                m_cudaDeviceID = current_device;
                m_nodeMask = deviceProp.luidDeviceNodeMask;
                cudaStreamCreateWithFlags(&videoBufferStream, cudaStreamNonBlocking);
                InitVKExts();
                ImportSemaphoreToCuda(m_texCopy.fence, m_texCopyExtSemaphore);
                ImportSemaphoreToCuda(m_texRendereComplete.fence, m_texRenderExtSemaphore);
                return;//current_device;
            }
        }
        else {
            ++devices_prohibited;
        }
    }

    //if (devices_prohibited == device_count) {
        Log::Write(Log::Level::Error, "CUDA error: No Vulkan-CUDA Interop capable GPU found.\n");
        std::exit(EXIT_FAILURE);
    //}
    return ;// -1;
}

#ifdef _WIN64  // For windows
HANDLE getVkImageMemHandle(VkDeviceMemory textureImageMemory, VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleType)
{
    const VkMemoryGetWin32HandleInfoKHR vkMemoryGetWin32HandleInfoKHR {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
        .pNext = nullptr,
        .memory = textureImageMemory,
        .handleType = (VkExternalMemoryHandleTypeFlagBitsKHR)externalMemoryHandleType,
    };
    HANDLE handle{};
    fpGetMemoryWin32HandleKHR(m_vkDevice, &vkMemoryGetWin32HandleInfoKHR, &handle);
    return handle;
}

HANDLE getVkSemaphoreHandle(VkExternalSemaphoreHandleTypeFlagBitsKHR externalSemaphoreHandleType, const VkSemaphore& semVkCuda)
{
    const VkSemaphoreGetWin32HandleInfoKHR vulkanSemaphoreGetWin32HandleInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
        .pNext = nullptr,
        .semaphore = semVkCuda,
        .handleType = externalSemaphoreHandleType
    };
    HANDLE handle{};
    fpGetSemaphoreWin32HandleKHR(m_vkDevice, &vulkanSemaphoreGetWin32HandleInfoKHR, &handle);
    return handle;
}

#else
int getVkImageMemHandle(VkDeviceMemory textureImageMemory, VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleType)
{
    if (externalMemoryHandleType != VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR)
        return -1;
    const VkMemoryGetFdInfoKHR vkMemoryGetFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .pNext = nullptr,
        .memory = textureImageMemory,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR
    };
    int fd = -1;
    fpGetMemoryFdKHR(m_vkDevice, &vkMemoryGetFdInfoKHR, &fd);
    return fd;
}

int getVkSemaphoreHandle(VkExternalSemaphoreHandleTypeFlagBitsKHR externalSemaphoreHandleType, const VkSemaphore& semVkCuda)
{
    if (externalSemaphoreHandleType != VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT)
        return -1;
    const VkSemaphoreGetFdInfoKHR vulkanSemaphoreGetFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .pNext = nullptr,
        .semaphore = semVkCuda,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR
    };
    int fd = -1;
    fpGetSemaphoreFdKHR(m_vkDevice, &vulkanSemaphoreGetFdInfoKHR, &fd);
    return fd; 
}
#endif

static constexpr inline cudaChannelFormatDesc CreateChannelDesc(const VkFormat fmt)
{
    switch (fmt) {
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_UNORM:
        return cudaCreateChannelDesc<std::uint8_t>();
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R10X6_UNORM_PACK16:
        return cudaCreateChannelDesc<std::uint16_t>();
    case VK_FORMAT_R8G8_UINT:
        return cudaCreateChannelDesc(8, 8, 0, 0, cudaChannelFormatKindUnsigned);
    case VK_FORMAT_R8G8_UNORM:// return cudaCreateChannelDesc<std::uint16_t>();
        return cudaCreateChannelDesc(8, 8, 0, 0, cudaChannelFormatKindUnsignedNormalized8X2);
    case VK_FORMAT_R16G16_UINT:
        return cudaCreateChannelDesc(16, 16, 0, 0, cudaChannelFormatKindUnsigned);
    case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
    case VK_FORMAT_R16G16_UNORM:
        return cudaCreateChannelDesc(16, 16, 0, 0, cudaChannelFormatKindUnsignedNormalized16X2);
    //case DXGI_FORMAT_P010:
    //case DXGI_FORMAT_P016:
    //    return cudaCreateChannelDesc(16, 32, 0, 0, cudaChannelFormatKindNone);//cudaChannelFormatKindNV12);
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        return cudaCreateChannelDescNV12();
    default: return cudaCreateChannelDesc(0, 0, 0, 0, cudaChannelFormatKindNone);
    }
}

inline void CudaVkSemaphoreSignal
(
    SemaphoreTimeline& extSemaphore,
    const cudaExternalSemaphore_t& cudaExtSemphore,
    cudaStream_t stream = 0
)
{
    const auto nextVal = extSemaphore.fenceValue.load() + 1;
    const cudaExternalSemaphoreSignalParams externalSemaphoreSignalParams{
        .params{.fence{.value = nextVal}},
        .flags = 0
    };
    if (cudaSignalExternalSemaphoresAsync(&cudaExtSemphore, &externalSemaphoreSignalParams, 1, stream) != cudaSuccess)
    {
        //Log::Write(Log::Level::Error, "m_texRendereComplete cudaSignalExternalSemaphoresAsync failed.");
        //CHECK(false);
    }
    extSemaphore.fenceValue.store(nextVal);
}

inline void CudaVkSemaphoreWait
(
    SemaphoreTimeline& extSemaphore,
    const cudaExternalSemaphore_t& cudaExtSemphore,
    cudaStream_t stream = 0
)
{
    const cudaExternalSemaphoreWaitParams externalSemaphoreWaitParams {
        .params{.fence{.value = extSemaphore.fenceValue.load()}},
        .flags = 0
    };
    if (cudaWaitExternalSemaphoresAsync(&cudaExtSemphore, &externalSemaphoreWaitParams, 1, stream) != cudaSuccess)
    {
        //Log::Write(Log::Level::Error, "cudaWaitExternalSemaphoresAsync failed.");
        //CHECK(false);
    }
}

void ClearVideoTexturesCUDA()
{
    m_videoTexturesCuda = {};
    //ClearVideoTextures();
}

virtual void CreateVideoTexturesCUDA(const CreateVideoTextureInfo& info) override
{
    CHECK_MSG((info.pixfmt != ALXR::YcbcrFormat::G8_B8_R8_3PLANE_420 &&
        info.pixfmt != ALXR::YcbcrFormat::G10X6_B10X6_R10X6_3PLANE_420), "3-Planes formats are not supported!");

    const auto pixelFmt = MapFormat(info.pixfmt);
    CreateVideoStreamPipeline(pixelFmt, info.ycbcrModel, info.ycbcrRange);

    std::array<VkFormat, 2> planeFmts{
        GetLumaFormat(pixelFmt),
        GetChromaFormat(pixelFmt)
    };
    for (std::size_t texIndex = 0; texIndex < m_videoTextures.size(); ++texIndex)
    {
        auto& vidTex = m_videoTextures[texIndex];
        auto& newSharedTex = m_videoTexturesCuda[texIndex];

        vidTex.width = info.width;
        vidTex.height = info.height;
        vidTex.format = pixelFmt;
        vidTex.texture.CreateExported
        (
            m_vkDevice, &m_memAllocator,
            static_cast<std::uint32_t>(info.width), static_cast<std::uint32_t>(info.height), pixelFmt,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_CREATE_DISJOINT_BIT
        );

        const VkSamplerYcbcrConversionInfo ycbcrConverInfo {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
            .pNext = nullptr,
            .conversion = m_videoStreamLayout.ycbcrSamplerConversion
        };
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = &ycbcrConverInfo,
            .image = vidTex.texture.texImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = pixelFmt,
            .subresourceRange {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };
        CHECK_VKCMD(vkCreateImageView(m_vkDevice, &viewInfo, nullptr, &vidTex.imageView));

        SetVideoTextureBindings(vidTex);

        for (std::size_t planeIndex = 0; planeIndex < newSharedTex.planeArrays.size(); ++planeIndex)
        {
            auto texMemory = vidTex.texture.texMemory[planeIndex];
            const auto totalImageMemSize = vidTex.texture.totalImageMemSizes[planeIndex];
            auto& externalMemory = newSharedTex.m_externalMemoryList[planeIndex];

            const cudaExternalMemoryHandleDesc cudaExtMemHandleDesc {
#ifdef _WIN64
                .type = /* IsWindows8OrGreater()*/ true ? cudaExternalMemoryHandleTypeOpaqueWin32 : cudaExternalMemoryHandleTypeOpaqueWin32Kmt,
                .handle{.win32{.handle = getVkImageMemHandle(texMemory, /*IsWindows8OrGreater()*/ true
                                            ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
                                            : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT)}},
#else
                .type = cudaExternalMemoryHandleTypeOpaqueFd,
                .handle{ .fd = getVkImageMemHandle(texMemory, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR) },
#endif
                .size = totalImageMemSize
            };
            if (cudaImportExternalMemory(&externalMemory, &cudaExtMemHandleDesc) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, "Failed to cudaImportExternalMemory.");
                CHECK(false);
            }

            const cudaExternalMemoryMipmappedArrayDesc cuExtmemMipDesc {
                .offset = 0,
                .formatDesc = CreateChannelDesc(planeFmts[planeIndex]),
                .extent = make_cudaExtent(info.width / (1 + planeIndex), info.height / (1 + planeIndex), 0),
                .flags = cudaArrayColorAttachment,
                .numLevels = 1
            };
            cudaMipmappedArray_t cuMipArray{};
            /*CheckCudaErrors*/
            if (cudaExternalMemoryGetMappedMipmappedArray(&cuMipArray, externalMemory, &cuExtmemMipDesc) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, "Failed to cudaExternalMemoryGetMappedMipmappedArray.");
                CHECK(false);
            }

            if (cudaGetMipmappedArrayLevel(&newSharedTex.planeArrays[planeIndex], cuMipArray, 0) != cudaSuccess)
            {
                Log::Write(Log::Level::Error, "Failed to cudaGetMipmappedArrayLevel.");
                CHECK(false);
            }
        }
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.   
    //cudaExternalSemaphore_t createTexSemp{};
    //D3D12FenceEvent createTexEvent{};
    //createTexEvent.CreateFence(m_device, D3D12_FENCE_FLAG_SHARED);
    //make_shared_semaphore(createTexEvent.fence, createTexSemp);
    //createTexEvent.WaitForGpu();
}

virtual void UpdateVideoTextureCUDA(const YUVBuffer& yuvBuffer) override
{
    //assert(false);
    const std::size_t freeIndex = m_currentVideoTex.load();
    const auto& videoTex = m_videoTexturesCuda[freeIndex];
    /*const*/ auto& newSharedTex = m_videoTextures[freeIndex];

    if (cudaMemcpy2DToArrayAsync
    (
        videoTex.planeArrays[0], 0, 0,
        yuvBuffer.luma.data, yuvBuffer.luma.pitch, newSharedTex.width * LumaSize(newSharedTex.format), yuvBuffer.luma.height,
        cudaMemcpyDeviceToDevice//,
        //videoBufferStream
    ) != cudaSuccess) {
        Log::Write(Log::Level::Error, "Failed to cudaMemcpy2DToArray.");
        CHECK(false);
    }

    if (cudaMemcpy2DToArrayAsync
    (
        videoTex.planeArrays[1], 0, 0,
        yuvBuffer.chroma.data, yuvBuffer.chroma.pitch, (newSharedTex.width / 2) * ChromaSize(newSharedTex.format), yuvBuffer.chroma.height,
        cudaMemcpyDeviceToDevice//,
        //videoBufferStream
    ) != cudaSuccess) {
        Log::Write(Log::Level::Error, "Failed to cudaMemcpy2DToArray.");
        CHECK(false);
    }
    //cudaStreamSynchronize(videoBufferStream);
    newSharedTex.frameIndex = yuvBuffer.frameIndex;

    //CudaVkSemaphoreSignal(m_texCopy, m_texCopyExtSemaphore, videoBufferStream);
    m_currentVideoTex.store((freeIndex + 1) % m_videoTextures.size());
    m_renderTex.store(freeIndex);
}
#endif
