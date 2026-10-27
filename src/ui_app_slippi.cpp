/* Controller-operated Slippi account and profile preparation.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_app.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"
#include "porpoise_paths.hpp"
#include <sys/stat.h>

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

std::string App::slippi_preferences_path() const
{
    return data_dir_ + "/slippi/" + (slippi_.game ? slippi_.game->id : "GALE01") + ".codes.json";
}

void App::refresh_slippi()
{
    std::string error;
    slippi::inspect_account(saves_dir_ + "/User", slippi_.account, error);
    if (!slippi::load_profile(PORPOISE_APP "/assets/slippi", slippi_.game ? slippi_.game->id : "GALE01",
                             slippi_.profile, error))
        slippi_.message = error;
    if (!slippi::load_preferences(slippi_preferences_path(), slippi_.prefs, error))
        slippi_.message = error;
}

void App::open_slippi(Game *game)
{
    slippi_ = SlippiPanel{};
    slippi_.open = true;
    slippi_.game = game;
    if (!game && lib_)
        for (auto &g : lib_->games())
            if (slippi::is_melee(g.id)) { slippi_.game = &g; break; }
    refresh_slippi();
    sfx(Sound::DetailsFlip);
}

void App::import_slippi(const std::string &path)
{
    if (slippi::import_account(path, saves_dir_ + "/User", slippi_.account, slippi_.message))
        slippi_.message = tr("Account imported, unverified. The Slippi backend must authenticate it during matchmaking.");
}

App::Action App::update_slippi(bool up, bool down, bool left, bool right)
{
    auto &p = slippi_;
    if (pressed(BtnCircle))
    {
        if (p.codes) { p.codes = false; p.row = 7; }
        else p.open = false;
        sfx(Sound::DetailsFlip);
        return Action::None;
    }
    const int n = p.codes ? int(p.profile.codes.size()) : 10;
    if (up || down)
    {
        p.row = n ? (p.row + n + (up ? -1 : 1)) % n : 0;
        p.message.clear();
        sfx(Sound::MenuScroll);
    }
    if (!p.codes && p.row == 0 && (left || right) && lib_)
    {
        std::vector<Game *> melee;
        for (auto &g : lib_->games()) if (slippi::is_melee(g.id)) melee.push_back(&g);
        auto it = std::find(melee.begin(), melee.end(), p.game);
        if (!melee.empty())
        {
            const int index = it == melee.end() ? 0 : int(it - melee.begin());
            p.game = melee[std::size_t((index + int(melee.size()) + (left ? -1 : 1)) % int(melee.size()))];
            p.message.clear();
            refresh_slippi();
        }
    }
    if (!pressed(BtnCross)) return Action::None;
    if (p.codes)
    {
        if (n == 0) return Action::None;
        const auto &c = p.profile.codes[std::size_t(p.row)];
        if (c.required) { p.message = tr("Required for Slippi Online."); return Action::None; }
        const auto before = p.prefs;
        p.prefs[list_name("Gecko", c.cheat.name)] = !slippi::enabled(c, p.prefs);
        mkdir((data_dir_ + "/slippi").c_str(), 0700);
        if (!slippi::save_preferences(slippi_preferences_path(), p.prefs, p.message)) p.prefs = before;
        else p.message = tr("Saved for the next Slippi profile preparation. Offline cheats are unchanged.");
        return Action::None;
    }
    switch (p.row)
    {
    case 0:
        p.message = tr("Left / Right selects a Melee image from your library.");
        break;
    case 1:
        if (!p.game) p.message = tr("Add a Melee disc image to your library first.");
        else
        {
            Game *game = p.game;
            p.open = false;
            return start_game(game, "", false, true);
        }
        break;
    case 2:
    {
        if (!p.game) { p.message = tr("Add a Melee disc image to your library first."); break; }
        slippi::Disc disc;
        slippi::Profile verified;
        if (!slippi::inspect_disc(p.game->path, disc, p.message)) break;
        if (!slippi::load_profile(PORPOISE_APP "/assets/slippi", disc.id, verified, p.message)) break;
        if (!slippi::inspect_account(saves_dir_ + "/User", p.account, p.message)) break;
        // Never start mandatory EXI codes on the ordinary offline core.
        p.message = tr(slippi::kBackendBlocker);
        break;
    }
    case 3:
        open_dialog(DialogKind::Info, tr("Slippi Sign In"),
                    tr("1. Open https://slippi.gg/online/enable on your phone or PC.\n"
                       "2. Sign in and download the official user.json.\n"
                       "3. Copy it to USB or transfer it to the PS5 over FTP.\n"
                       "4. Choose Import Existing Account here and select the file.\n"
                       "Importing does not verify your play key. This build still needs the Slippi core port."), "");
        break;
    case 4:
        p.open = false;
        browse_images_ = false;
        browse_slippi_ = true;
        open_browser("");
        break;
    case 5:
        open_dialog(DialogKind::SlippiSignOut, tr("Sign Out of Slippi?"),
                    tr("Remove Porpoise's imported account from this console? The original export is unchanged."),
                    tr("Sign Out"));
        break;
    case 6:
        p.message = tr(slippi::kBackendBlocker);
        break;
    case 7:
        if (p.profile.codes.empty()) refresh_slippi();
        if (!p.profile.codes.empty()) { p.codes = true; p.row = 0; }
        break;
    case 8:
    {
        if (!p.game) { p.message = tr("Select a Melee image first."); break; }
        slippi::Disc disc;
        if (!slippi::inspect_disc(p.game->path, disc, p.message)) break;
        slippi::Profile verified;
        if (!slippi::load_profile(PORPOISE_APP "/assets/slippi", disc.id, verified, p.message)) break;
        mkdir((data_dir_ + "/slippi").c_str(), 0700);
        if (slippi::write_profile(verified, p.prefs, data_dir_ + "/slippi/" + disc.id + "r2.ini", p.message))
            p.message = tr("Official profile prepared separately. Header check passed; full image verification and the Slippi core port are still required.");
        break;
    }
    case 9:
        open_dialog(DialogKind::Info, tr("Slippi Network Diagnostics"),
                    tr("Backend: unavailable in the bundled libretro core.\n"
                       "Account: an imported file is unverified until accepted by matchmaking.\n"
                       "Unranked / Direct / Teams / Ranked: unavailable.\n"
                       "Latency and NAT status: not measured.\n"
                       "See SLIPPI_PS5_STATUS.md for the core, Rust and networking blockers."), "");
        break;
    }
    return Action::None;
}

void App::draw_slippi()
{
    if (!slippi_.open) return;
    const auto &p = slippi_;
    Gfx &g = *g_;
    const auto hint = rgba(0x8A96C8);
    g.set_layer();
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.75f), 1, 0);
    const float x = 170, y = 80, w = 1580;
    g.panel(x, y, w, 880, rgba(0x13307F, 0.97f), 1, kR, kIcy, 2.4f, 12);
    g.text_mid(Font::Bold, ts(42), x + 48, y + 55, kWhite, Align::Left,
               tr(p.codes ? "Slippi Gecko Codes" : "Slippi Online"));
    g.text_mid(Font::SemiBold, ts(24), x + w - 48, y + 55, hint, Align::Right, tr("Core port required"));
    if (!p.codes)
    {
        const std::string account = p.account.imported
            ? tr("Account: Imported, unverified") + "  |  " + p.account.display_name + "  |  " + p.account.connect_code
            : tr("Account: Not imported");
        g.text_mid(Font::SemiBold, ts(25), x + 48, y + 115, kSoft, Align::Left,
                   fit(g, Font::SemiBold, ts(25), account, w - 96));
    }
    else
        g.text_mid(Font::SemiBold, ts(24), x + 48, y + 115, kSoft, Align::Left,
                   tr("Official online profile only. Required codes are locked; changes apply when preparing the next profile."));

    const char *const actions[] = {"Melee Image (Left / Right)", "Play Offline", "Play Slippi Online",
        "Sign In Instructions", "Import Existing Account", "Sign Out", "Reauthenticate", "Gecko Codes",
        "Prepare Official Profile", "Network Diagnostics"};
    const int n = p.codes ? int(p.profile.codes.size()) : 10;
    constexpr int shown = 7;
    const int first = std::clamp(p.row - shown + 1, 0, std::max(0, n - shown));
    float ry = y + 162;
    for (int i = first; i < n && i < first + shown; ++i, ry += 72)
    {
        const bool focus = p.row == i;
        g.panel(x + 30, ry, w - 60, 64, focus ? rgba(0x1F63F0, 0.9f) : rgba(0x07102E, 0.6f),
                1, kR, focus ? kIcy : rgba(0x3D5AB0), focus ? 2 : 1);
        std::string label = p.codes ? list_name("Gecko", p.profile.codes[std::size_t(i)].cheat.name).substr(1) : tr(actions[i]);
        g.text_mid(Font::SemiBold, ts(27), x + 56, ry + 32, focus ? kWhite : kSoft, Align::Left,
                   fit(g, Font::SemiBold, ts(27), label, w - 260));
        if (p.codes)
        {
            const auto &c = p.profile.codes[std::size_t(i)];
            const bool on = slippi::enabled(c, p.prefs);
            const float sx = x + w - 154, cy = ry + 32;
            draw_cheat_switch(g, sx, cy, 90, 40, on, focus, c.required);
        }
        else if (i == 0)
            g.text_mid(Font::Regular, ts(22), x + w - 54, ry + 32, hint, Align::Right,
                       p.game ? p.game->id : tr("None"));
        else if (i == 2 || i == 6)
            g.text_mid(Font::Regular, ts(22), x + w - 54, ry + 32, hint, Align::Right, tr("Unavailable"));
    }
    std::string note = p.message;
    if (note.empty() && p.codes && !p.profile.codes.empty())
    {
        const auto &c = p.profile.codes[std::size_t(p.row)];
        note = c.required ? tr("Required for Slippi Online.") : c.description;
    }
    if (note.empty() && !p.codes && p.row == 0 && p.game) note = p.game->file;
    if (note.empty()) note = tr("Online play is not available in this build. Play Offline uses your normal Porpoise settings.");
    float ny = y + 720;
    for (const auto &line : wrap(g, Font::Regular, ts(25), note, w - 110, 3))
    {
        g.text_mid(Font::Regular, ts(25), x + 55, ny, kSoft, Align::Left, line);
        ny += 34;
    }
    g.text_mid(Font::Regular, ts(22), x + w - 50, y + 850, hint, Align::Right,
               std::to_string(n ? p.row + 1 : 0) + " / " + std::to_string(n));
    g.set_layer();
    drawing_dialog_ = true;
    draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Select"}}, {{Glyph::Circle, "Back"}}, "");
    drawing_dialog_ = false;
}
} // namespace porpoise::ui
