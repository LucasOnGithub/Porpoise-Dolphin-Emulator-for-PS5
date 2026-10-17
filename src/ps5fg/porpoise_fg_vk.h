/* Porpoise - the Vulkan commands ps5-framegen calls, as function pointers.
 * Porpoise reaches the driver through pointers it loads itself (VK_NO_PROTOTYPES,
 * src/porpoise_vk.cpp), so ps5_framegen.cpp's calls resolve to these, declared
 * in its own namespace. porpoise::vk loads them when frame generation starts.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#define PORPOISE_FG_INSTANCE_FUNCS(X)                                                            \
    X(vkGetPhysicalDeviceMemoryProperties)                                                       \
    X(vkGetPhysicalDeviceProperties)

#define PORPOISE_FG_DEVICE_FUNCS(X)                                                              \
    X(vkAllocateDescriptorSets)                                                                  \
    X(vkAllocateMemory)                                                                          \
    X(vkBindBufferMemory)                                                                        \
    X(vkBindImageMemory)                                                                         \
    X(vkCmdBindDescriptorSets)                                                                   \
    X(vkCmdBindPipeline)                                                                         \
    X(vkCmdClearColorImage)                                                                      \
    X(vkCmdDispatch)                                                                             \
    X(vkCmdFillBuffer)                                                                           \
    X(vkCmdPipelineBarrier)                                                                      \
    X(vkCmdResetQueryPool)                                                                       \
    X(vkCreateBuffer)                                                                            \
    X(vkCreateComputePipelines)                                                                  \
    X(vkCreateDescriptorPool)                                                                    \
    X(vkCreateDescriptorSetLayout)                                                               \
    X(vkCreateImage)                                                                             \
    X(vkCreateImageView)                                                                         \
    X(vkCreatePipelineLayout)                                                                    \
    X(vkCreateQueryPool)                                                                         \
    X(vkCreateSampler)                                                                           \
    X(vkCreateShaderModule)                                                                      \
    X(vkDestroyBuffer)                                                                           \
    X(vkDestroyDescriptorPool)                                                                   \
    X(vkDestroyDescriptorSetLayout)                                                              \
    X(vkDestroyImage)                                                                            \
    X(vkDestroyImageView)                                                                        \
    X(vkDestroyPipeline)                                                                         \
    X(vkDestroyPipelineLayout)                                                                   \
    X(vkDestroyQueryPool)                                                                        \
    X(vkDestroySampler)                                                                          \
    X(vkDestroyShaderModule)                                                                     \
    X(vkFreeMemory)                                                                              \
    X(vkGetBufferMemoryRequirements)                                                             \
    X(vkGetDeviceProcAddr)                                                                       \
    X(vkGetImageMemoryRequirements)                                                              \
    X(vkGetQueryPoolResults)                                                                     \
    X(vkMapMemory)                                                                               \
    X(vkUnmapMemory)                                                                             \
    X(vkUpdateDescriptorSets)

namespace ps5::framegen
{
#define PORPOISE_FG_DECLARE(name) extern PFN_##name name;
PORPOISE_FG_INSTANCE_FUNCS(PORPOISE_FG_DECLARE)
PORPOISE_FG_DEVICE_FUNCS(PORPOISE_FG_DECLARE)
#undef PORPOISE_FG_DECLARE

/* Loads the table for `device`; false (and the first missing name) when one
 * is missing. */
bool load_vulkan(VkInstance instance, VkDevice device, PFN_vkGetInstanceProcAddr get_instance_proc,
                 PFN_vkGetDeviceProcAddr get_device_proc, const char **missing);
} // namespace ps5::framegen
