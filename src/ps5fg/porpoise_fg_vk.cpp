/* Porpoise - ps5-framegen's Vulkan commands (porpoise_fg_vk.h).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_fg_vk.h"

namespace ps5::framegen
{
#define PORPOISE_FG_DEFINE(name) PFN_##name name = nullptr;
PORPOISE_FG_INSTANCE_FUNCS(PORPOISE_FG_DEFINE)
PORPOISE_FG_DEVICE_FUNCS(PORPOISE_FG_DEFINE)
#undef PORPOISE_FG_DEFINE

bool load_vulkan(VkInstance instance, VkDevice device, PFN_vkGetInstanceProcAddr get_instance_proc,
                 PFN_vkGetDeviceProcAddr get_device_proc, const char **missing)
{
    *missing = nullptr;
#define PORPOISE_FG_LOAD_I(name)                                                                 \
    name = reinterpret_cast<PFN_##name>(get_instance_proc(instance, #name));                    \
    if (!name && !*missing)                                                                      \
        *missing = #name;
#define PORPOISE_FG_LOAD_D(name)                                                                 \
    name = reinterpret_cast<PFN_##name>(get_device_proc(device, #name));                        \
    if (!name && !*missing)                                                                      \
        *missing = #name;
    PORPOISE_FG_INSTANCE_FUNCS(PORPOISE_FG_LOAD_I)
    PORPOISE_FG_DEVICE_FUNCS(PORPOISE_FG_LOAD_D)
#undef PORPOISE_FG_LOAD_I
#undef PORPOISE_FG_LOAD_D
    return *missing == nullptr;
}
} // namespace ps5::framegen
