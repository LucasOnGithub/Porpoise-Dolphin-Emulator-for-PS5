/* Porpoise - frame generation (3.1, beta): when to show in-between frames.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_framegen.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

#include "porpoise_vk.hpp"
#include "ps5fg/ps5_framegen_pacing.h"
#include "trace.hpp"

namespace porpoise::framegen
{
namespace
{
std::mutex g_mutex; /* the config and the stats: set from the menus, read by the loop and the overlay */
Config g_config;
Stats g_stats;

long long now_ns()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

/* What is still to be shown of the game's last frame: further in-between
 * frames and then the frame itself, each on its own refresh. Refreshes are
 * counted from the first in-between frame's (slot 0). */
struct Step
{
    int slot = 0;
    int generated = 0; /* 1, 2: that further in-between frame; 0: the game's frame */
};

struct Game
{
    ps5::framegen::pacer pacer;
    bool tried = false;   /* the interpolator was asked for this game */
    bool ready = false;
    std::string error;
    unsigned ticks_since = 0; /* runs since the last new frame */
    unsigned already = 0;     /* presents since then (the pacer's `already`) */
    bool engaged = false;     /* the last new frame was given to the interpolator */
    /* The interpolator's last frame is no neighbour of the next one (frame
     * generation was off, fast forward, the menu, a load): start it over. */
    bool force_reset = true;
    /* The display's queue had a frame waiting when one was presented (its
     * acquire waited): the next in-between frame is left out once, so the
     * queue drains instead of holding every frame a refresh longer. */
    bool backlog = false;
    unsigned drained = 0;
    bool always_engaged = false;
    /* Steady 30: a frame of the game came that isn't taken yet (it waits
     * for the second run since the last one taken). */
    bool held = false;
    Step steps[4];
    int step_count = 0, step_next = 0;
    int window = 0;    /* the slot this run's first present lands on */
    int last_slot = 0; /* the slot of the last present made */
    bool flipped = true; /* that present is on screen already (a run starts after its vblank) */
    /* The second's counts. */
    long long second_start = 0;
    unsigned new_frames = 0, presents = 0, generated = 0;
    double fg_ns = 0;
    unsigned runs = 0;
    std::uint64_t flips_at = 0;
    bool flips_known = false;
    unsigned pauses = 0;
    std::FILE *csv = nullptr;
    long long started = 0;
    std::string debug_dir;
};
Game g;

void drop_pending()
{
    if (g.step_count > 0)
    {
        g.step_count = g.step_next = 0;
        porpoise::vk::fg::advance(); /* the game's frame never got its turn; it is the previous one now */
    }
}

/* One present of what the steps hold, at `slot`. */
void present_step(const Step &step)
{
    if (step.generated > 0)
    {
        if (porpoise::vk::fg::present_generated(unsigned(step.generated)))
            ++g.generated;
    }
    else
    {
        porpoise::vk::fg::present_real();
        porpoise::vk::fg::advance();
        g.already = 1; /* the pacer counts the presents from the game's frame on */
    }
    ++g.presents;
}

/* This run's share of the steps: those whose refresh comes before the run's
 * own vblanks are over, each after the vblanks before it. Returns the vblanks
 * waited. */
int run_steps(int vblanks_per_tick)
{
    int waited = 0;
    while (g.step_next < g.step_count)
    {
        const Step &step = g.steps[g.step_next];
        if (step.slot >= g.window + vblanks_per_tick)
            break; /* a later run's */
        /* A present lands on the next refresh. One made in this run is still
         * waiting for its own: its vblank first. */
        for (int wait = step.slot - g.last_slot - (g.flipped ? 1 : 0); wait > 0; --wait)
        {
            if (!porpoise::vk::wait_vblank())
                break;
            ++waited;
        }
        present_step(step);
        g.last_slot = step.slot;
        g.flipped = false;
        ++g.step_next;
    }
    if (g.step_next >= g.step_count)
        g.step_count = g.step_next = 0;
    return waited;
}

State from_pacer(ps5::framegen::pacer_state state)
{
    switch (state)
    {
    case ps5::framegen::pacer_state::Warming:
        return State::Warming;
    case ps5::framegen::pacer_state::NoRoom:
        return State::NoRoom;
    case ps5::framegen::pacer_state::Unsteady:
        return State::Unsteady;
    case ps5::framegen::pacer_state::NotNominal:
        return State::FastForward;
    case ps5::framegen::pacer_state::Paused:
        return State::SafetyPause;
    case ps5::framegen::pacer_state::Generating:
        return State::Generating;
    }
    return State::Off;
}

unsigned g_changes = 0;           /* state changes since the last log line */
long long g_state_since = 0;      /* when the state last changed */

void set_state(State state)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_stats.state != state)
    {
        /* A change after a state that held a while is worth a line; a game
         * flicking between two (a load, a pause, an uneven stretch) is
         * counted in the second's line instead. */
        const long long now = now_ns();
        if (now - g_state_since > 2000000000LL || state == State::Unavailable)
        {
            char line[96];
            std::snprintf(line, sizeof line, "framegen: %s -> %s", state_name(g_stats.state), state_name(state));
            ps5::debug::mark(line);
        }
        ++g_changes;
        g_state_since = now;
    }
    g_stats.state = state;
}

void open_csv()
{
    if (g.csv || g.debug_dir.empty())
        return;
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char name[64];
    std::strftime(name, sizeof name, "/framegen-%Y%m%d-%H%M%S.csv", &tm);
    g.csv = std::fopen((g.debug_dir + name).c_str(), "w");
    if (g.csv)
        std::fprintf(g.csv, "ms,new,ticks,state,engaged,reset,presents,shown,waited,present_ms\n");
}

void second(const Run &run, const Config &cfg)
{
    const long long now = now_ns();
    if (g.second_start == 0)
    {
        g.second_start = now;
        std::uint64_t flips = 0;
        int pending = 0;
        g.flips_known = porpoise::vk::flip_status(flips, pending);
        g.flips_at = flips;
        return;
    }
    const double span = double(now - g.second_start) / 1e9;
    if (span < 1.0)
        return;
    double gpu_mean = 0, gpu_max = 0, flow = 0;
    unsigned gpu_count = 0;
    const bool gpu = porpoise::vk::fg::gpu_time(gpu_mean, gpu_max, flow, gpu_count);
    std::uint64_t flips = 0;
    int pending = 0;
    const bool flips_known = porpoise::vk::flip_status(flips, pending);
    Stats copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stats.game_fps = g.new_frames / span;
        g_stats.shown_fps = g.presents / span;
        g_stats.generated_fps = g.generated / span;
        if (gpu)
        {
            g_stats.gpu_ms = gpu_mean;
            g_stats.gpu_max_ms = gpu_max;
            g_stats.flow_ms = flow;
        }
        else if (g.generated == 0)
            g_stats.gpu_ms = g_stats.gpu_max_ms = g_stats.flow_ms = 0;
        g_stats.vblank_hz = run.vblank_hz;
        g_stats.vblanks_per_tick = run.vblanks_per_tick;
        g_stats.refreshes = g.pacer.refreshes();
        g_stats.steady = g.pacer.steady_frames();
        g_stats.flips_per_s = flips_known && g.flips_known ? double(flips - g.flips_at) / span : -1.0;
        g_stats.flips_pending = pending;
        g_stats.fg_ms = g.runs ? g.fg_ns / g.runs / 1e6 : 0.0;
        g_stats.added_lag_ms = g_stats.state == State::Generating && g_stats.game_fps > 1.0 ? 500.0 / g_stats.game_fps : 0.0;
        g_stats.multiplier = cfg.multiplier;
        g_stats.pauses = g.pauses;
        g_stats.error = g.error;
        copy = g_stats;
    }
    g.flips_at = flips;
    g.flips_known = flips_known;
    g.second_start = now;
    g.new_frames = g.presents = g.generated = g.runs = 0;
    g.fg_ns = 0;
    if (cfg.on)
    {
        /* Every 5 seconds while it's on, whatever it's doing (every second
         * with the log on): one line in the trace, for reports. */
        static int quiet = 0;
        if (cfg.log || ++quiet >= 5)
        {
            quiet = 0;
            unsigned changes = 0;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                changes = g_changes;
                g_changes = 0;
            }
            char line[400];
            std::snprintf(line, sizeof line,
                          "framegen: %s, game %.1f fps (%.0f%% speed), shown %.1f, generated %.1f; gpu %.2f ms (max %.2f, "
                          "flow %.2f); %.2f Hz x%d, %.2f refreshes a frame, %u/8 steady, %u warm; flips %.1f/s (%d "
                          "pending); run %.2f ms (max %.2f), present %.2f, pace %.2f, framegen %.2f; %d late; %u "
                          "changes, %u drained",
                          state_name(copy.state), copy.game_fps, run.speed, copy.shown_fps, copy.generated_fps,
                          copy.gpu_ms, copy.gpu_max_ms, copy.flow_ms, copy.vblank_hz, copy.vblanks_per_tick,
                          copy.refreshes, copy.steady, g.pacer.warm_frames(), copy.flips_per_s, copy.flips_pending,
                          copy.run_ms, copy.run_max_ms, copy.present_ms, copy.pace_ms, copy.fg_ms, copy.misses,
                          changes, g.drained);
            ps5::debug::mark(line);
            g.drained = 0;
        }
    }
}
} // namespace

const char *state_name(State state)
{
    switch (state)
    {
    case State::Off:
        return "Off";
    case State::Unavailable:
        return "Unavailable";
    case State::NeedsVsync:
        return "Needs V-Sync";
    case State::FastForward:
        return "Fast Forward";
    case State::Warming:
        return "Warming Up";
    case State::NoRoom:
        return "No Room";
    case State::Unsteady:
        return "Uneven Frame Rate";
    case State::SafetyPause:
        return "Paused (Too Slow)";
    case State::Generating:
        return "Generating";
    }
    return "Off";
}

void set_config(const Config &config)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_config = config;
    g_config.multiplier = std::clamp(g_config.multiplier, 2, 4);
}

Config config()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_config;
}

Stats stats()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_stats;
}

void begin_game(const char *debug_dir)
{
    end_game();
    g.debug_dir = debug_dir ? debug_dir : "";
    g.started = now_ns();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_stats = Stats{};
}

void end_game()
{
    if (g.csv)
        std::fclose(g.csv);
    g.csv = nullptr;
    g.pacer = ps5::framegen::pacer{};
    g.tried = g.ready = false;
    g.error.clear();
    g.ticks_since = g.already = 0;
    g.engaged = g.always_engaged = false;
    g.force_reset = true;
    g.held = false;
    g.backlog = false;
    g.drained = 0;
    g.step_count = g.step_next = 0;
    g.window = g.last_slot = 0;
    g.second_start = 0;
    g.new_frames = g.presents = g.generated = g.runs = 0;
    g.fg_ns = 0;
    g.pauses = 0;
}

void paused()
{
    drop_pending();
    g.engaged = g.always_engaged = false;
    g.held = false;
    g.force_reset = true; /* a state loaded, a disc changed... */
}

void note_loop(double run_ms, double run_max_ms, double present_ms, double pace_ms, int misses, double fps)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_stats.run_ms = run_ms;
    g_stats.run_max_ms = run_max_ms;
    g_stats.present_ms = present_ms;
    g_stats.pace_ms = pace_ms;
    g_stats.misses = misses;
    g_stats.loop_fps = fps;
}

bool present(const Run &run)
{
    const Config cfg = config();
    const long long start = now_ns();
    ++g.ticks_since;
    ++g.runs;
    /* The frame this run goes by. With Steady 30 the game's frames are taken
     * two runs apart at the most often (the newest one then), so a game that
     * runs anywhere between 30 and 60 is shown at an even 30, with the
     * in-between frames taking it to 60 (or 120). */
    bool fresh = run.new_frame;
    if (cfg.on && cfg.steady30)
    {
        g.held = g.held || run.new_frame;
        fresh = g.held && g.ticks_since >= 2;
        if (fresh)
            g.held = false;
    }
    else
        g.held = false;
    if (fresh)
        ++g.new_frames;
    auto leave = [&](State state) {
        /* Not this run: the loop presents the game's frame as usual. */
        drop_pending();
        g.engaged = g.always_engaged = false;
        g.force_reset = true;
        if (state == State::Off && g.ready)
        {
            /* Turned off in the game: its memory goes now (made again if
             * it's turned back on). */
            porpoise::vk::fg::stop();
            g.tried = g.ready = false;
        }
        if (fresh)
        {
            g.ticks_since = 0;
            g.already = 0;
        }
        ++g.already;
        ++g.presents;
        set_state(state);
        second(run, cfg);
        return false;
    };
    if (!cfg.on)
        return leave(State::Off);
    if (!g.tried)
    {
        g.tried = true;
        g.ready = porpoise::vk::fg::start(g.error);
    }
    if (!g.ready)
        return leave(State::Unavailable);
    if (!run.vsynced || run.vblanks_per_tick < 1 || run.vblank_hz < 20.0)
        return leave(State::NeedsVsync);
    if (!run.nominal)
        return leave(State::FastForward);
    porpoise::vk::fg::set_debug_view(cfg.debug_view);
    if (cfg.log)
        open_csv();

    /* This run starts a new stretch of the display's refreshes: the steps
     * still to come move along by this run's vblanks. */
    const int k = run.vblanks_per_tick;
    g.window += k;
    g.last_slot = g.window - 1;
    g.flipped = true;

    int waited = 0;
    bool engaged_now = false, reset = false;
    unsigned presents = 0;
    const char *shown = "none";
    State state = State::Generating;
    if (fresh)
    {
        drop_pending(); /* a new frame before the last one's turn came */
        const unsigned ticks = g.ticks_since;
        g.ticks_since = 0;
        const double now_s = double(start) / 1e9;
        const double refreshes = ticks * run.vblank_hz / std::max(run.content_hz, 1.0);
        const unsigned fit = unsigned(refreshes + 0.03);
        ps5::framegen::pacer_decision d =
            g.pacer.frame(ticks, g.already, run.content_hz, run.vblank_hz, now_s, cfg.safety_net ? run.speed : 100.0,
                          run.nominal);
        if (g.pacer.take_event() == ps5::framegen::pacer_event::Paused)
            ++g.pauses;
        state = from_pacer(g.pacer.state());
        if (cfg.pacing == 1 && state != State::SafetyPause)
        {
            /* Whenever the frame has the room, steady or not. */
            d.engaged = fit >= 2;
            d.reset = !g.always_engaged || ticks > ps5::framegen::pacer::kGapTicks;
            const unsigned left = fit > g.already ? fit - g.already : 0;
            d.presents = std::min(std::max(1u, fit / 2), left);
            g.always_engaged = d.engaged;
            state = d.engaged ? State::Generating : State::NoRoom;
        }
        if (!d.engaged)
        {
            g.already = 0;
            return leave(state);
        }
        engaged_now = true;
        reset = d.reset || g.force_reset;
        /* How many in-between frames: one, shown for d.presents refreshes; or
         * with the multiplier up to multiplier - 1, as many as the frame's
         * refreshes have room for, every picture (the game's included) on
         * screen for as many refreshes as the others: 3x at 30 fps on 120 Hz
         * would show the game's frame twice as long, so it makes one, for two. */
        unsigned frames = d.presents > 0 ? 1 : 0;
        unsigned shown_each = d.presents;
        if (cfg.multiplier > 2 && d.presents > 0 && fit >= 2)
        {
            const unsigned left = fit > g.already ? fit - g.already : 0;
            unsigned f = std::min(unsigned(cfg.multiplier - 1), std::min(left, fit - 1));
            while (f > 1 && fit % (f + 1) != 0)
                --f;
            if (f >= 1)
            {
                frames = f;
                shown_each = fit / (f + 1);
            }
        }
        g.already = 0;
        if (g.backlog && frames > 0)
        {
            frames = 0; /* this time the game's frame alone */
            g.backlog = false;
            ++g.drained;
        }
        const porpoise::vk::fg::Shown first = porpoise::vk::fg::frame(
            run.width, run.height, run.aspect, run.filter, run.strength, reset, std::max(frames, 1u), frames > 0);
        /* Its present found no image free for a couple of milliseconds:
         * frames are queued for the display. */
        if (first == porpoise::vk::fg::Shown::Generated && porpoise::vk::last_present_wait_ms() > 2.0)
            g.backlog = true;
        if (first == porpoise::vk::fg::Shown::Nothing)
            return leave(State::Unavailable);
        g.force_reset = false;
        g.engaged = true;
        ++g.presents;
        if (first == porpoise::vk::fg::Shown::Real)
        {
            porpoise::vk::fg::advance();
            g.already = 1;
            shown = "real";
            presents = 1;
        }
        else
        {
            ++g.generated;
            shown = "generated";
            presents = 1;
            /* Slots from here: this present is slot 0. */
            g.window = 0;
            g.last_slot = 0;
            g.flipped = false;
            g.step_count = g.step_next = 0;
            /* Each picture on screen for shown_each refreshes: the further
             * in-between frames, then the game's. */
            const int each = int(std::max(shown_each, 1u));
            for (unsigned i = 1; i < frames && g.step_count < 3; ++i)
                g.steps[g.step_count++] = Step{int(i) * each, int(i)};
            g.steps[g.step_count++] = Step{int(std::max(frames, 1u)) * each, 0};
            /* The window of this run's vblanks runs from slot 0. */
            const unsigned before = g.presents;
            waited = run_steps(k);
            presents += g.presents - before;
        }
    }
    else if (g.step_count > 0)
    {
        /* A run that repeats the game's frame: the steps due in it. */
        const unsigned before = g.presents;
        waited = run_steps(k);
        presents = g.presents - before;
        shown = presents ? "step" : "none";
        engaged_now = true;
    }
    else if (g.engaged && g.ticks_since > ps5::framegen::pacer::kGapTicks)
    {
        /* No new frame for a while (a load, a still screen): the loop's own
         * presents again, so what is drawn over the game (the FPS counter,
         * notices) goes on moving. */
        return leave(State::Warming);
    }
    else if (g.engaged)
    {
        /* The game's frame is on screen and stays: the repeat presents nothing. */
        engaged_now = true;
        state = from_pacer(g.pacer.state());
        if (cfg.pacing == 1)
            state = State::Generating;
    }
    else
    {
        /* Not generating (a gap ended it): the pacer may say Generating for
         * the next new frame, but this one is the game's own. */
        const State s = from_pacer(g.pacer.state());
        return leave(s == State::Generating ? State::Warming : s);
    }

    set_state(state);
    const long long took = now_ns() - start;
    g.fg_ns += double(took);
    if (g.csv)
        std::fprintf(g.csv, "%.2f,%d,%u,%s,%d,%d,%u,%s,%d,%.3f\n", double(start - g.started) / 1e6,
                     fresh ? 1 : run.new_frame ? 2 : 0, g.ticks_since, state_name(state), engaged_now ? 1 : 0, reset ? 1 : 0,
                     presents, shown, waited, double(took) / 1e6);
    second(run, cfg);
    return true;
}
} // namespace porpoise::framegen
