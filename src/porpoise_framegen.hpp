/* Porpoise - frame generation (3.1, alpha): when to show in-between frames.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The in-between frames come from ps5-framegen (src/ps5fg, Swordpdf's, as in
 * PS5SX2): FSR 3's optical flow on the game's last two frames and a frame
 * halfway between them, drawn by porpoise::vk::fg. This part decides when:
 * a frame of the game that stays on screen for two of the display's refreshes
 * or more (a 30 fps game at 60 Hz, a 60 fps game with the PS5 set to 120 Hz)
 * gets the frame between it and the one before, shown first, then the game's
 * own. ps5-framegen's pacer counts the game's ticks (a retro_run is one of
 * the emulated machine's fields) and only generates at a steady rate.
 *
 * The game loop stays as it is: each run, present() either presents this
 * run's frames itself (true) or leaves the present to the loop (false).
 * Presents land one per vblank: a frame meant for a later refresh waits for
 * the vblanks before it (porpoise::vk::wait_vblank), here or in the next run.
 * So it needs V-Sync on the display's vblank (porpoise::pacer). */
#pragma once

#include <string>

namespace porpoise::framegen
{
struct Config
{
    bool on = false;         /* Settings > Video > Frame Generation (alpha) */
    int multiplier = 2;      /* developer options: 2, 3 or 4 frames for each of the game's */
    int pacing = 0;          /* developer options: 0 steady only (the pacer), 1 whenever there is room */
    bool safety_net = true;  /* developer options: none for a while when they cost the game its speed */
    bool debug_view = false; /* developer options: the interpolation's choices in colour */
    bool log = false;        /* developer options: a CSV of every run, in the debug folder */
};
void set_config(const Config &config);
Config config();

enum class State
{
    Off,
    Unavailable, /* the interpolator couldn't be made (Stats::error) */
    NeedsVsync,  /* the pacer isn't on the display's vblank */
    FastForward,
    Warming,     /* the start, or after a pause or a load */
    NoRoom,      /* the game's frames have fewer than two refreshes each */
    Unsteady,    /* the game's frame rate isn't even */
    SafetyPause, /* they cost the game its speed: none for a while */
    Generating,
};
/* The state's name in English, for the statistics (translated where shown). */
const char *state_name(State state);

struct Stats
{
    State state = State::Off;
    std::string error;
    double game_fps = 0;      /* new frames of the game a second */
    double shown_fps = 0;     /* presents a second, the generated ones included */
    double generated_fps = 0; /* generated frames shown a second */
    double gpu_ms = 0, gpu_max_ms = 0, flow_ms = 0; /* the interpolator's GPU time per frame */
    double vblank_hz = 0;
    int vblanks_per_tick = 0;
    double refreshes = 0;     /* the display's refreshes per game frame (the pacer's median) */
    unsigned steady = 0;      /* of the last 8 frames, how many are the usual length */
    double flips_per_s = -1;  /* the display's flips a second (-1: not known) */
    int flips_pending = 0;
    double run_ms = 0, run_max_ms = 0, present_ms = 0, pace_ms = 0; /* the loop, per run */
    int misses = 0;           /* runs late for their vblank, last second */
    double loop_fps = 0;
    double fg_ms = 0;         /* time in present() per run (drawing and the vblank waits in it) */
    double added_lag_ms = 0;  /* the half frame interpolation shows the game's frame later */
    int multiplier = 2;
    unsigned pauses = 0;      /* the safety net's pauses this game */
};
Stats stats();

/* The game starts and ends (the interpolator lives as long as the game's
 * device; this resets the pacing and the counts). */
void begin_game(const char *debug_dir);
void end_game();
/* The in-game menu opened: what was still to be shown is dropped. */
void paused();

struct Run
{
    bool new_frame = false;   /* the core handed over a new frame in this run */
    unsigned width = 0, height = 0;
    float aspect = 0;
    int filter = 0;
    float strength = 0;
    bool vsynced = false;     /* the pacer waits on the display's vblank */
    int vblanks_per_tick = 0; /* the display's vblanks per run: 1 at 60 Hz, 2 at 120 */
    double vblank_hz = 0;     /* the display's own rate, measured */
    double content_hz = 60;   /* the game's */
    double speed = 100;       /* percent of the game's own speed, lately */
    bool nominal = true;      /* no fast forward */
};
/* This run's presents: true when frame generation made them (the loop then
 * presents nothing), false when the loop presents as it always does. */
bool present(const Run &run);
/* The loop's own times, about once a second, for the statistics. */
void note_loop(double run_ms, double run_max_ms, double present_ms, double pace_ms, int misses, double fps);
} // namespace porpoise::framegen
