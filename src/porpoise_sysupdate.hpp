/* Porpoise - Wii System Update: the list of the Wii's system titles for a
 * region (the Wii Menu, its IOS and channels), asked of the update service
 * Dolphin's own Online System Update uses, over the console's HTTPS. The core
 * then downloads each title from Nintendo's servers and installs it as
 * Dolphin does (Boot.cpp, PORPOISE_SYSTEM_UPDATE). Porpoise ships none of it.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace porpoise::sysupdate
{
struct Job
{
    std::atomic<int> state{0}; /* 0 asking, 1 the list is in, 2 failed */
    std::atomic<bool> cancelled{false}; /* the player stopped waiting: its list isn't written */
    std::mutex m;
    std::string error; /* for the player (state 2) */
};

/* The titles for region (USA, EUR, JPN or KOR) into out_path, for the core.
 * Run it on a worker. */
void fetch(const std::string &region, const std::string &out_path, Job &job);
} // namespace porpoise::sysupdate
