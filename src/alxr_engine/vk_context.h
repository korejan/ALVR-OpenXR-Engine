#pragma once
#ifndef ALXR_VK_CONTEXT_H
#define ALXR_VK_CONTEXT_H

//#ifdef XR_USE_GRAPHICS_API_VULKAN
#include "pch.h"
#include <utility>
#include <algorithm>
#include <vector>
#include <string_view>

namespace ALXR::Vk {

struct DeviceFeatures;
struct DeviceProperties;

inline constexpr const std::uint32_t NullQueueFamilyIndex = (std::uint32_t)-1;
inline constexpr const std::uint32_t NullQueueIndex = NullQueueFamilyIndex;

struct QueueFamily final {
    std::uint32_t familyIndex{ NullQueueFamilyIndex };
    std::uint32_t queueCount{ 0 };
    VkQueueFlags queueFlags{ 0 };
#ifdef VK_KHR_video_queue
    VkVideoCodecOperationFlagsKHR videoCodecOperations{ VK_VIDEO_CODEC_OPERATION_NONE_KHR };
#endif
    constexpr bool IsValid() const noexcept { return familyIndex != NullQueueFamilyIndex; }
};
using QueueFamilyList = std::vector<QueueFamily>;

struct QueueIndex final {
    std::uint32_t queueFamilyIndex;
    std::uint32_t queueIndex;

    constexpr bool IsValid() const noexcept {
        return queueFamilyIndex != NullQueueFamilyIndex &&
               queueIndex != NullQueueIndex;
    }

    constexpr bool operator<(const QueueIndex& other) const noexcept {
        return queueFamilyIndex < other.queueFamilyIndex && queueIndex < other.queueIndex;
    }
    constexpr bool operator==(const QueueIndex& other) const noexcept {
        return queueFamilyIndex == other.queueFamilyIndex && queueIndex == other.queueIndex;
    }
    constexpr bool operator!=(const QueueIndex& other) const noexcept { return !(*this == other); }
};

struct AVQueueFamilyList {
    // @avQueueFamilies:
    //   only contains enabled queue families / instances used for ffmpeg's vulkan decode support.
    //   depending on the device, *may* contain @VkContext::graphicsQueueIndex but avoided if possible.
    QueueFamilyList queueFamilies{};

// legacy AV queue family indices
    struct {
        std::uint32_t graphics{ NullQueueFamilyIndex };
        std::uint32_t decode{ NullQueueFamilyIndex };
        std::uint32_t compute{ NullQueueFamilyIndex };
        std::uint32_t transfer{ NullQueueFamilyIndex };
    } queueFamilyIndex;
};

struct VkContext final {
    VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
    VkDevice         device{ VK_NULL_HANDLE };
    VkInstance       instance{ VK_NULL_HANDLE };
    VkAllocationCallbacks* allocator{ nullptr };
    
#ifdef VK_KHR_synchronization2
    PFN_vkCmdPipelineBarrier2KHR vkCmdPipelineBarrier2KHR{nullptr};
#endif

    // @graphicsQueueIndex:
    //   main graphics queue index used by alxr's main render thread, if other components
    QueueIndex graphicsQueue { (std::uint32_t)-1, (std::uint32_t)-1, };

    using ExtensionList = std::vector<const char*>;
    ExtensionList instanceExtensions{};
    ExtensionList deviceExtensions{};

    AVQueueFamilyList avQueueFamilies{};

	constexpr bool IsValid() const {
		return	physicalDevice != VK_NULL_HANDLE &&
				device != VK_NULL_HANDLE &&
				instance != VK_NULL_HANDLE;
	}

    bool IsInstanceExtEnabled(std::string_view extName) const;
    bool IsDeviceExtEnabled(std::string_view extName) const;

    bool GetDeviceProperties(DeviceProperties& properties) const;
    bool GetSupportedDeviceFeatures(DeviceFeatures& features) const;    
    bool GetRequiredDeviceFeatures(DeviceFeatures& features) const;

    template < typename VkFn >
    VkFn GetInstanceProcAddr(const char* fnName) const;
    template < typename VkFn >
    VkFn GetDeviceProcAddr(const char* fnName) const;

    void InitExtFunctions();
};

struct DevicePropertiesBase {
    VkPhysicalDeviceMultiviewPropertiesKHR multiview = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES_KHR,
        .pNext = nullptr,
    };
    VkPhysicalDeviceProperties2KHR properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
        .pNext = nullptr,
    };
    constexpr DevicePropertiesBase() noexcept = default;
    constexpr DevicePropertiesBase(const DevicePropertiesBase&) noexcept = default;
    constexpr DevicePropertiesBase(DevicePropertiesBase&&) noexcept = default;
    constexpr DevicePropertiesBase& operator=(const DevicePropertiesBase&) noexcept = default;
    constexpr DevicePropertiesBase& operator=(DevicePropertiesBase&&) noexcept = default;
};

struct DeviceProperties final : DevicePropertiesBase {
    using BaseT = DevicePropertiesBase;

    void InitNextPtrs(const DeviceProperties* src = nullptr);    

    DeviceProperties() noexcept
    : BaseT{} { InitNextPtrs(); }

    DeviceProperties(const DeviceProperties& src) noexcept
    : BaseT{ static_cast<const BaseT&>(src) } { InitNextPtrs(&src); }

    DeviceProperties(DeviceProperties&& src) noexcept
    : DeviceProperties{ static_cast<const DeviceProperties&>(src) } {}

    DeviceProperties& operator=(const DeviceProperties& src) noexcept {
        static_cast<BaseT&>(*this) = static_cast<const BaseT&>(src);
        InitNextPtrs(&src);
        return *this;
    }

    DeviceProperties& operator=(DeviceProperties&& src) noexcept {
        return *this = static_cast<const DeviceProperties&>(src);
    }
};

struct DeviceFeaturesBase {
    VkPhysicalDeviceMultiviewFeatures multiview = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR,
        .pNext = nullptr,
    };
#ifdef VK_EXT_pipeline_creation_cache_control
    // Used by MetaXR simulator
    VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT pipelineCreationCacheControl = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT,
        .pNext = nullptr,
    };
#endif
#if !defined(XR_USE_PLATFORM_ANDROID) && !defined(XR_DISABLE_DECODER_THREAD)
#ifdef VK_KHR_timeline_semaphore
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext = nullptr,
    };
#endif
#ifdef VK_KHR_video_maintenance1
    VkPhysicalDeviceVideoMaintenance1FeaturesKHR videoMaintenance1 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR,
        .pNext = nullptr,
    };
#endif
#ifdef VK_KHR_video_maintenance2
    VkPhysicalDeviceVideoMaintenance2FeaturesKHR videoMaintenance2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_2_FEATURES_KHR,
        .pNext = nullptr,
    };
#endif
#ifdef VK_EXT_shader_object
    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObject = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
        .pNext = nullptr,
    };
#endif
#ifdef VK_NV_optical_flow
    VkPhysicalDeviceOpticalFlowFeaturesNV opticalFlow = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV,
        .pNext = nullptr,
    };
#endif
#ifdef VK_KHR_cooperative_matrix
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR coopMatrix = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR,
        .pNext = nullptr,
    };
#endif
#ifdef VK_EXT_shader_atomic_float
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloat = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT,
        .pNext = nullptr,
    };
#endif
#ifdef VK_EXT_descriptor_buffer
    VkPhysicalDeviceDescriptorBufferFeaturesEXT  descriptorBuffer = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
        .pNext = nullptr,
    };
#endif
#ifdef VK_EXT_host_image_copy
    VkPhysicalDeviceHostImageCopyFeaturesEXT hostCopy = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT,
        .pNext = nullptr,
    };
#endif
#ifdef VK_KHR_shader_subgroup_rotate
    VkPhysicalDeviceShaderSubgroupRotateFeaturesKHR subgroupRotate = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES_KHR,
        .pNext = nullptr,
    };
#endif
#ifdef VK_KHR_shader_expect_assume
    VkPhysicalDeviceShaderExpectAssumeFeaturesKHR expectAssume = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EXPECT_ASSUME_FEATURES_KHR,
        .pNext = nullptr,
    };
#endif
#ifdef VK_VERSION_1_3
    VkPhysicalDeviceVulkan13Features featuresV13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = nullptr,
    };
#endif
#ifdef VK_VERSION_1_2
    VkPhysicalDeviceVulkan12Features featuresV12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = nullptr,
    };
#endif
#endif
#ifdef VK_VERSION_1_2
    VkPhysicalDeviceVulkan11Features featuresV11 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = nullptr,
    };
#endif
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = nullptr,
    };

    constexpr DeviceFeaturesBase() noexcept = default;
    constexpr DeviceFeaturesBase(const DeviceFeaturesBase&) noexcept = default;
    constexpr DeviceFeaturesBase(DeviceFeaturesBase&&) noexcept = default;
    constexpr DeviceFeaturesBase& operator=(const DeviceFeaturesBase&) noexcept = default;
    constexpr DeviceFeaturesBase& operator=(DeviceFeaturesBase&&) noexcept = default;
};

struct DeviceFeatures final : public DeviceFeaturesBase {
    
    using BaseT = DeviceFeaturesBase;
    void InitNextPtrs(const DeviceFeatures* const src = nullptr);
    
    DeviceFeatures() noexcept
    : BaseT{} { InitNextPtrs(); }

    DeviceFeatures(const DeviceFeatures& src) noexcept
    : BaseT{static_cast<const BaseT&>(src)} { InitNextPtrs(&src); }

    DeviceFeatures(DeviceFeatures&& src) noexcept
    : DeviceFeatures{static_cast<const DeviceFeatures&>(src)} {}

    DeviceFeatures& operator=(const DeviceFeatures& src) noexcept {
        static_cast<BaseT&>(*this) = static_cast<const BaseT&>(src);
        InitNextPtrs(&src);
        return *this;
    }

    DeviceFeatures& operator=(DeviceFeatures&& src) noexcept {
        return *this = static_cast<const DeviceFeatures&>(src);
    }
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
inline bool VkContext::IsInstanceExtEnabled(std::string_view extName) const {
    if (extName.empty())
        return false;
    return std::find(instanceExtensions.begin(), instanceExtensions.end(), extName) != instanceExtensions.end();
}

inline bool VkContext::IsDeviceExtEnabled(std::string_view extName) const {
    if (extName.empty())
        return false;
    return std::find(deviceExtensions.begin(), deviceExtensions.end(), extName) != deviceExtensions.end();
}

template <typename VkFn>
inline VkFn VkContext::GetInstanceProcAddr(const char* fnName) const {
    return reinterpret_cast<VkFn>(vkGetInstanceProcAddr(instance, fnName));
}

template <typename VkFn>
inline VkFn VkContext::GetDeviceProcAddr(const char* fnName) const {
    return reinterpret_cast<VkFn>(vkGetDeviceProcAddr(device, fnName));
}

inline bool VkContext::GetDeviceProperties(DeviceProperties& properties) const {
#if 1
    const auto fpGetPhysicalDeviceProperties2
        = GetInstanceProcAddr<PFN_vkGetPhysicalDeviceProperties2KHR>("vkGetPhysicalDeviceProperties2KHR");
    if (fpGetPhysicalDeviceProperties2 == nullptr) {
        return false;
    }
#else
    constexpr const auto fpGetPhysicalDeviceProperties2 =
        vkGetPhysicalDeviceProperties2;
#endif
    fpGetPhysicalDeviceProperties2(physicalDevice, &properties.properties);
    return true;
}

}
#endif
