#pragma once

/**
@file
@brief Includes the Vulkan API with project-wide preprocessor configurations
*/

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_EXCEPTIONS
// Used to allow aggregate initialization for structs
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#define VULKAN_HPP_ASSERT_ON_RESULT

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_format_traits.hpp>
#include <vulkan/vulkan_hash.hpp>

#include <ymir/core/types.hpp>

#include <span>

#if VK_HEADER_VERSION >= 301
using VkDispatchLoaderDynamic = vk::detail::DispatchLoaderDynamic;
#else
using VkDispatchLoaderDynamic = vk::DispatchLoaderDynamic;
#endif

namespace ymir::gpu::vulkan {

vk::UniqueInstance CreateInstance(std::span<const char *const> instanceExtensions,
                                  const VkDispatchLoaderDynamic &dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER);

} // namespace ymir::gpu::vulkan