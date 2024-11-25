#include "vk_context.h"
#include "common.h"

#include <cassert>
#include <cstring>

namespace ALXR::Vk {
namespace {
template <typename Tp>
inline void CopyVkStruct(Tp & dst, const Tp & src) {
    constexpr const std::size_t size = sizeof(VkBaseOutStructure);
    static_assert(size == sizeof(VkBaseInStructure));
    static_assert(size < sizeof(Tp));
    std::memcpy(&dst + size, &src + size, sizeof(Tp) - size);
}

template <typename Tp, typename Tq>
inline void VkChainUnlink(Tp& root, Tq& toUnlink) {
    auto rootB = reinterpret_cast<VkBaseOutStructure*>(&root);
    auto toUnlinkB = reinterpret_cast<VkBaseOutStructure*>(&toUnlink);
    while (rootB != nullptr) {
        if (rootB->pNext == toUnlinkB) {
            rootB->pNext = toUnlinkB->pNext;
            toUnlinkB->pNext = nullptr;
            return;
        }
        rootB = rootB->pNext;
    }
}
}

bool VkContext::GetSupportedDeviceFeatures(DeviceFeatures& features) const {
#if 1
    const auto fpGetPhysicalDeviceFeatures2
        = (PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2KHR");
    if (fpGetPhysicalDeviceFeatures2 == nullptr)
        return false;
#else
    constexpr const auto fpGetPhysicalDeviceFeatures2 = vkGetPhysicalDeviceFeatures2KHR;
#endif
    fpGetPhysicalDeviceFeatures2(physicalDevice, &features.features2);

#if (defined(VK_VERSION_1_3) || defined(VK_VERSION_1_2))
    const VkPhysicalDeviceVulkan11Features& featuresV11 = features.featuresV11;
    assert(features.multiview.pNext == nullptr);
    features.multiview = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR,
        .pNext = nullptr,
        .multiview = featuresV11.multiview,
        .multiviewGeometryShader = featuresV11.multiviewGeometryShader,
        .multiviewTessellationShader = featuresV11.multiviewTessellationShader,
    };
    const VkPhysicalDeviceVulkan12Features& featuresV12 = features.featuresV12;
    assert(features.timeline.pNext == nullptr);
    features.timeline.timelineSemaphore = featuresV12.timelineSemaphore;
#endif
    return true;
}

bool VkContext::GetRequiredDeviceFeatures(ALXR::Vk::DeviceFeatures& requiredFeatures) const {
    ALXR::Vk::DeviceFeatures supportedFeatures{};
    if (!GetSupportedDeviceFeatures(supportedFeatures)) {
        Log::Write(Log::Level::Error, "Failed to get device features");
        return false;
    }

    auto& dstFeature = requiredFeatures.features2.features;
    const auto& srcFeature = supportedFeatures.features2.features;

    // shaderStorageImageMultisample used by meta-link runtime, not enabling this causes vk validation errors...
    dstFeature.shaderStorageImageMultisample = srcFeature.shaderStorageImageMultisample;
    dstFeature.shaderImageGatherExtended = srcFeature.shaderImageGatherExtended;    
    dstFeature.shaderStorageImageReadWithoutFormat = srcFeature.shaderStorageImageReadWithoutFormat;
    dstFeature.shaderStorageImageWriteWithoutFormat = srcFeature.shaderStorageImageWriteWithoutFormat;
    dstFeature.fragmentStoresAndAtomics = srcFeature.fragmentStoresAndAtomics;
    dstFeature.vertexPipelineStoresAndAtomics = srcFeature.vertexPipelineStoresAndAtomics;
    dstFeature.shaderInt64 = srcFeature.shaderInt64;
    dstFeature.shaderInt16 = srcFeature.shaderInt16;
    dstFeature.shaderFloat64 = srcFeature.shaderFloat64;

#ifdef VK_VERSION_1_2
    auto& dstFeatureV11 = requiredFeatures.featuresV11;
    const auto& srcFeatureV11 = supportedFeatures.featuresV11;

    if (srcFeatureV11.samplerYcbcrConversion == VK_FALSE) {
        Log::Write(Log::Level::Error, "device feature samplerYcbcrConversion is not supported, this is required to run!");
        return false;
    }
    dstFeatureV11.samplerYcbcrConversion = srcFeatureV11.samplerYcbcrConversion;
    dstFeatureV11.multiview = srcFeatureV11.multiview;
    dstFeatureV11.storagePushConstant16 = srcFeatureV11.storagePushConstant16;
    dstFeatureV11.storageBuffer16BitAccess = srcFeatureV11.storageBuffer16BitAccess;
    dstFeatureV11.uniformAndStorageBuffer16BitAccess = srcFeatureV11.uniformAndStorageBuffer16BitAccess;

    auto& dstFeatureV12 = requiredFeatures.featuresV12;
    const auto& srcFeatureV12 = supportedFeatures.featuresV12;

    if (srcFeatureV12.timelineSemaphore == VK_FALSE) {
        Log::Write(Log::Level::Error, "device feature timelineSemaphore is not supported, this is required to run!");
        return false;
    }
    dstFeatureV12.timelineSemaphore = srcFeatureV12.timelineSemaphore;
    dstFeatureV12.bufferDeviceAddress = srcFeatureV12.bufferDeviceAddress;
    dstFeatureV12.hostQueryReset = srcFeatureV12.hostQueryReset;
    dstFeatureV12.storagePushConstant8 = srcFeatureV12.storagePushConstant8;
    dstFeatureV12.shaderInt8 = srcFeatureV12.shaderInt8;
    dstFeatureV12.storageBuffer8BitAccess = srcFeatureV12.storageBuffer8BitAccess;
    dstFeatureV12.uniformAndStorageBuffer8BitAccess = srcFeatureV12.uniformAndStorageBuffer8BitAccess;
    dstFeatureV12.shaderFloat16 = srcFeatureV12.shaderFloat16;
    dstFeatureV12.shaderSharedInt64Atomics = srcFeatureV12.shaderSharedInt64Atomics;
    dstFeatureV12.vulkanMemoryModel = srcFeatureV12.vulkanMemoryModel;
    dstFeatureV12.vulkanMemoryModelDeviceScope = srcFeatureV12.vulkanMemoryModelDeviceScope;
    dstFeatureV12.hostQueryReset = srcFeatureV12.hostQueryReset;
#endif

#ifdef VK_VERSION_1_3
    auto& dstFeatureV13 = requiredFeatures.featuresV13;
    const auto& srcFeatureV13 = supportedFeatures.featuresV13;

    dstFeatureV13.dynamicRendering = srcFeatureV13.dynamicRendering;
    dstFeatureV13.maintenance4 = srcFeatureV13.maintenance4;
    dstFeatureV13.synchronization2 = srcFeatureV13.synchronization2;
    dstFeatureV13.computeFullSubgroups = srcFeatureV13.computeFullSubgroups;
    dstFeatureV13.shaderZeroInitializeWorkgroupMemory = srcFeatureV13.shaderZeroInitializeWorkgroupMemory;
#endif

//#ifdef VK_KHR_multiview
    if (IsDeviceExtEnabled(VK_KHR_MULTIVIEW_EXTENSION_NAME)) {
        auto& dstMultiview = requiredFeatures.multiview;
        const auto& srcMultiview = supportedFeatures.multiview;
        dstMultiview.multiview = srcMultiview.multiview;
        //dstMultiview.multiviewGeometryShader = srcMultiview.multiviewGeometryShader;
        //dstMultiview.multiviewTessellationShader = srcMultiview.multiviewTessellationShader;
    } else {
        VkChainUnlink(requiredFeatures.features2, requiredFeatures.multiview);
    }
//#endif

    if (IsDeviceExtEnabled(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
        auto& dstTimeline = requiredFeatures.timeline;
        const auto& srcTimeline = supportedFeatures.timeline;
        dstTimeline.timelineSemaphore = srcTimeline.timelineSemaphore;
        //dstMultiview.multiviewGeometryShader = srcMultiview.multiviewGeometryShader;
        //dstMultiview.multiviewTessellationShader = srcMultiview.multiviewTessellationShader;
    } else {
        VkChainUnlink(requiredFeatures.features2, requiredFeatures.timeline);
    }

#ifdef VK_KHR_video_maintenance1
    if (IsDeviceExtEnabled(VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME)) {
        auto& dstVideoMaintenance1 = requiredFeatures.videoMaintenance1;
        const auto& srcVideoMaintenance1 = supportedFeatures.videoMaintenance1;
        dstVideoMaintenance1.videoMaintenance1 = srcVideoMaintenance1.videoMaintenance1;
    } else {
        VkChainUnlink(requiredFeatures.features2, requiredFeatures.videoMaintenance1);
    }
#endif

#ifdef VK_EXT_descriptor_buffer
    if (IsDeviceExtEnabled(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)) {
        auto& dstDescriptorBuffer = requiredFeatures.descriptorBuffer;
        const auto& srcDescriptorBuffer = supportedFeatures.descriptorBuffer;
        dstDescriptorBuffer.descriptorBuffer = srcDescriptorBuffer.descriptorBuffer;
        dstDescriptorBuffer.descriptorBufferPushDescriptors = srcDescriptorBuffer.descriptorBufferPushDescriptors;
    } else {
        VkChainUnlink(requiredFeatures.features2, requiredFeatures.descriptorBuffer);
    }
#endif

#ifdef VK_EXT_shader_atomic_float
    if (IsDeviceExtEnabled(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME)) {
        auto& dstAtomicFloat = requiredFeatures.atomicFloat;
        const auto& srcAtomicFloat = supportedFeatures.atomicFloat;
        dstAtomicFloat.shaderBufferFloat32Atomics = srcAtomicFloat.shaderBufferFloat32Atomics;
        dstAtomicFloat.shaderBufferFloat32AtomicAdd = srcAtomicFloat.shaderBufferFloat32AtomicAdd;
    } else {
        VkChainUnlink(requiredFeatures.features2, requiredFeatures.atomicFloat);
    }
#endif

#ifdef VK_KHR_cooperative_matrix
    if (IsDeviceExtEnabled(VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME)) {
        auto& dstCoopMatrix = requiredFeatures.coopMatrix;
        const auto& srcCoopMatrix = supportedFeatures.coopMatrix;
        dstCoopMatrix.cooperativeMatrix = srcCoopMatrix.cooperativeMatrix;
    } else {
        VkChainUnlink(requiredFeatures.features2, requiredFeatures.coopMatrix);
    }
#endif

#ifdef VK_NV_optical_flow
    if (IsDeviceExtEnabled(VK_NV_OPTICAL_FLOW_EXTENSION_NAME)) {
        auto& dstOpticalFlow = requiredFeatures.opticalFlow;
        const auto& srcOpticalFlow = supportedFeatures.opticalFlow;
        dstOpticalFlow.opticalFlow = srcOpticalFlow.opticalFlow;
    } else {
        VkChainUnlink(requiredFeatures.features2, requiredFeatures.opticalFlow);
    }
#endif

#ifdef VK_EXT_shader_object
    if (IsDeviceExtEnabled(VK_EXT_SHADER_OBJECT_EXTENSION_NAME)) {
        auto& dstShaderObject = requiredFeatures.shaderObject;
        const auto& srcShaderObject = supportedFeatures.shaderObject;
        dstShaderObject.shaderObject = srcShaderObject.shaderObject;
    } else {
        VkChainUnlink(requiredFeatures.features2, requiredFeatures.shaderObject);
    }
#endif
    return true;
}

#ifndef DF_SET_NEXT_PTR
#define DF_SET_NEXT_PTR(ft)                     \
        ft.pNext = nullptr;                     \
        if (src == nullptr || src->ft.pNext) {  \
            ft.pNext = nextptr;                 \
            nextptr = &ft;                      \
        }
#endif

void DeviceProperties::InitNextPtrs(const DeviceProperties* src /*= nullptr*/) {
    void* nextptr = &multiview;
    DF_SET_NEXT_PTR(properties);
}

void DeviceFeatures::InitNextPtrs(const DeviceFeatures* const src /*= nullptr*/) {

#if (defined(VK_VERSION_1_3) || defined(VK_VERSION_1_2))
    void* nextptr = nullptr;
#else
    void* nextptr = &multiview;
#endif

#if !defined(XR_USE_PLATFORM_ANDROID) && !defined(XR_DISABLE_DECODER_THREAD)

#if !(defined(VK_VERSION_1_3) || defined(VK_VERSION_1_2)) && defined(VK_KHR_timeline_semaphore)
    DF_SET_NEXT_PTR(timeline);
#endif

#ifdef VK_KHR_video_maintenance1
    DF_SET_NEXT_PTR(videoMaintenance1);
#endif
#ifdef VK_EXT_shader_object
    DF_SET_NEXT_PTR(shaderObject);
#endif
#ifdef VK_NV_optical_flow
    DF_SET_NEXT_PTR(opticalFlow);
#endif
#ifdef VK_KHR_cooperative_matrix
    DF_SET_NEXT_PTR(coopMatrix);
#endif
#ifdef VK_EXT_shader_atomic_float
    DF_SET_NEXT_PTR(atomicFloat);
#endif
#ifdef VK_EXT_descriptor_buffer
    DF_SET_NEXT_PTR(descriptorBuffer);
#endif
#ifdef VK_VERSION_1_3
    DF_SET_NEXT_PTR(featuresV13);
#endif
#ifdef VK_VERSION_1_2
    DF_SET_NEXT_PTR(featuresV12);
#endif
#endif
#ifdef VK_VERSION_1_2
    DF_SET_NEXT_PTR(featuresV11);
#endif
    DF_SET_NEXT_PTR(features2);
}
#undef DF_SET_NEXT_PTR

}
