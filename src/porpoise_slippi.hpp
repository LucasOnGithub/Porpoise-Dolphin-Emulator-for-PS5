/* Slippi preparation. No matchmaking or authenticated state is inferred from disk.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <map>
#include <string>
#include <vector>
#include "ui_cheats.hpp"

namespace porpoise::slippi
{
inline constexpr const char *kLoginUrl = "https://slippi.gg/online/enable";
inline constexpr const char *kBackendBlocker =
    "Slippi core integration is experimental and unavailable in this PS5 build. Online play is unavailable.";

struct Account
{
    bool imported = false;
    std::string display_name, connect_code; // Never expose uid or playKey to the UI.
};
std::string config_folder(const std::string &dolphin_user_root);
std::string account_path(const std::string &dolphin_user_root);
// Creates the FTP destination without changing existing offline User permissions.
bool prepare_ftp_account_folder(const std::string &user_root, std::string &error);
// Reads only the canonical FTP destination, validates it, and privately normalizes
// the export. Success means loaded, not authenticated by Slippi matchmaking.
bool load_ftp_account(const std::string &user_root, Account &out, std::string &error);
bool inspect_account(const std::string &user_root, Account &out, std::string &error);
bool import_account(const std::string &from, const std::string &user_root, Account &out, std::string &error);
bool sign_out(const std::string &user_root, std::string &error);

struct Disc
{
    std::string id;
    unsigned revision = 0;
    // A header check is not a full image hash or a guarantee against desyncs.
    bool header_compatible = false;
};
bool is_melee(const std::string &id);
bool inspect_disc(const std::string &path, Disc &out, std::string &error);

struct Code
{
    ui::Cheat cheat;
    bool required = false;
    std::string description;
};
using Preferences = std::map<std::string, bool>;
struct Profile
{
    std::string id, source; // source is the verified, unmodified official INI.
    std::vector<Code> codes;
};
bool load_profile(const std::string &asset_dir, const std::string &id, Profile &out, std::string &error);
bool enabled(const Code &code, const Preferences &preferences);
bool load_preferences(const std::string &path, Preferences &out, std::string &error);
bool save_preferences(const std::string &path, const Preferences &prefs, std::string &error);
// Generates a standalone profile; never appends to offline GameSettings.
// Unknown preferences are refused. Required codes cannot be disabled.
bool write_profile(const Profile &profile, const Preferences &prefs, const std::string &path, std::string &error);
} // namespace porpoise::slippi
