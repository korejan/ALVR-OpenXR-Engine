// Copyright (c) 2017-2025 The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <utility>

#include "pch.h"
#include "common.h"
#include "options.h"
#include "platformdata.h"
#include "graphicsplugin.h"

// Graphics API factories are forward declared here.
#ifdef XR_USE_GRAPHICS_API_VULKAN
std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_VulkanLegacy(const std::shared_ptr<Options>& options,
                                                                   std::shared_ptr<IPlatformPlugin> platformPlugin);

std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_Vulkan(const std::shared_ptr<Options>& options,
                                                             std::shared_ptr<IPlatformPlugin> platformPlugin);
#endif
#ifdef XR_USE_GRAPHICS_API_D3D11
std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_D3D11(const std::shared_ptr<Options>& options,
                                                            std::shared_ptr<IPlatformPlugin> platformPlugin);
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_D3D12(const std::shared_ptr<Options>& options,
                                                            std::shared_ptr<IPlatformPlugin> platformPlugin);
#endif

std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_Headless(const std::shared_ptr<Options>& options,
                                                               std::shared_ptr<IPlatformPlugin> platformPlugin);


namespace {
using GraphicsPluginFactory = std::function<std::shared_ptr<IGraphicsPlugin>(const std::shared_ptr<Options>& options,
                                                                             std::shared_ptr<IPlatformPlugin> platformPlugin)>;

const std::map<std::string, GraphicsPluginFactory, IgnoreCaseStringLess> graphicsPluginMap = {
#ifdef XR_USE_GRAPHICS_API_VULKAN
    {"Vulkan",
     [](const std::shared_ptr<Options>& options, std::shared_ptr<IPlatformPlugin> platformPlugin) {
         return CreateGraphicsPlugin_VulkanLegacy(options, std::move(platformPlugin));
     }},
    {"Vulkan2",
     [](const std::shared_ptr<Options>& options, std::shared_ptr<IPlatformPlugin> platformPlugin) {
         return CreateGraphicsPlugin_Vulkan(options, std::move(platformPlugin));
     }},
#endif
#ifdef XR_USE_GRAPHICS_API_D3D11
    {"D3D11",
     [](const std::shared_ptr<Options>& options, std::shared_ptr<IPlatformPlugin> platformPlugin) {
         return CreateGraphicsPlugin_D3D11(options, std::move(platformPlugin));
     }},
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
    {"D3D12",
     [](const std::shared_ptr<Options>& options, std::shared_ptr<IPlatformPlugin> platformPlugin) {
         return CreateGraphicsPlugin_D3D12(options, std::move(platformPlugin));
     }},
#endif
    {"Headless",
     [](const std::shared_ptr<Options>& options, std::shared_ptr<IPlatformPlugin> platformPlugin) {
         return CreateGraphicsPlugin_Headless(options, std::move(platformPlugin));
     }},
};
}  // namespace

std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin(const std::shared_ptr<Options>& options,
                                                      std::shared_ptr<IPlatformPlugin> platformPlugin) {
    if (options->GraphicsPlugin.empty()) {
        throw std::invalid_argument("No graphics API specified");
    }

    const auto apiIt = graphicsPluginMap.find(options->GraphicsPlugin);
    if (apiIt == graphicsPluginMap.end()) {
        throw std::invalid_argument(Fmt("Unsupported graphics API '%s'", options->GraphicsPlugin.c_str()));
    }

    return apiIt->second(options, std::move(platformPlugin));
}
