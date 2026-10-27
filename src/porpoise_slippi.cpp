/* Slippi account import and isolated, integrity-checked code profiles.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_slippi.hpp"
#include "porpoise_atomic.hpp"
#include "porpoise_sha256.hpp"

#include <cerrno>
#include <cstring>
#include <set>
#include <sstream>
#include <sys/stat.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#define JSON_NOEXCEPTION 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "../third_party/nlohmann/json.hpp"
#pragma GCC diagnostic pop

namespace porpoise::slippi
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t kAccountLimit = 16 * 1024;
constexpr std::size_t kCodeLimit = 2 * 1024 * 1024;
const std::map<std::string, std::string> kDigests = {
#include "slippi_codes.inc"
};
const char *const kRequired[] = {"$Required: General Codes", "$Required: Slippi Recording", "$Required: Slippi Online"};

bool fail(std::string &error, const char *message)
{
    error = message; // Errors are fixed strings, never parser exceptions or input.
    return false;
}

bool read_bounded(const std::string &path, std::size_t limit, std::string &out, bool private_file = false)
{
    out.clear();
#ifndef _WIN32
    // Nonblocking first: opening a FIFO must not hang the UI. No credential symlinks.
    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        (private_file && fchmod(fd, 0600) != 0) ||
        st.st_size <= 0 || std::uint64_t(st.st_size) > limit)
    {
        close(fd);
        return false;
    }
    std::FILE *f = fdopen(fd, "rb");
    if (!f)
    {
        close(fd);
        return false;
    }
#else
    (void)private_file; // Desktop permissions use the existing Windows account/ACL.
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
#endif
    char buf[4096];
    std::size_t n;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof buf, f)) != 0)
    {
        if (out.size() + n > limit)
        {
            ok = false;
            break;
        }
        out.append(buf, n);
    }
    ok = !std::ferror(f) && ok && !out.empty();
    std::fclose(f);
    return ok;
}

bool parse_json(const std::string &text, Json &out)
{
    // Bound nesting before invoking the DOM parser, including unknown fields.
    int depth = 0;
    bool quoted = false, escape = false;
    for (unsigned char c : text)
    {
        if (quoted)
        {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') quoted = false;
        }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 16) return false; }
        else if (c == '}' || c == ']') { if (--depth < 0) return false; }
    }
    if (depth || quoted)
        return false;
    bool duplicate = false;
    std::vector<std::set<std::string>> keys(18);
    out = Json::parse(text, [&](int d, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start)
            keys[std::size_t(d + 1)].clear();
        if (event == Json::parse_event_t::key && !keys[std::size_t(d)].insert(value.get<std::string>()).second)
            duplicate = true;
        return true;
    }, false);
    return !duplicate && !out.is_discarded() && out.is_object();
}

bool string_field(const Json &j, const char *name, std::size_t max)
{
    const auto it = j.find(name);
    if (it == j.end() || !it->is_string())
        return false;
    const auto &s = it->get_ref<const std::string &>();
    if (s.empty() || s.size() > max)
        return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f)
            return false;
    return true;
}

bool parse_account(const std::string &text, Account &summary, Json &normalized)
{
    Json j;
    if (text.size() > kAccountLimit || !parse_json(text, j) ||
        !string_field(j, "uid", 256) || !string_field(j, "playKey", 512) ||
        !string_field(j, "displayName", 128) || !string_field(j, "connectCode", 64))
        return false;
    const auto code = j["connectCode"].get<std::string>();
    const auto hash = code.find('#');
    if (hash == std::string::npos || hash == 0 || code.size() - hash < 2 || code.size() - hash > 6)
        return false;
    for (std::size_t i = hash + 1; i < code.size(); ++i)
        if (code[i] < '0' || code[i] > '9') return false;
    if (j.count("latestVersion") && !j["latestVersion"].is_string())
        return false;
    normalized = {{"uid", j["uid"]}, {"playKey", j["playKey"]}, {"displayName", j["displayName"]},
                  {"connectCode", j["connectCode"]}, {"latestVersion", j.value("latestVersion", std::string())}};
    summary = {true, j["displayName"].get<std::string>(), code};
    return true;
}

bool private_folder(const std::string &path)
{
#ifndef _WIN32
    struct stat st;
    if (lstat(path.c_str(), &st) != 0)
    {
        if (errno != ENOENT || mkdir(path.c_str(), 0700) != 0)
            return false;
    }
    else if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return false;
    return chmod(path.c_str(), 0700) == 0;
#else
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) || mkdir(path.c_str(), 0700) == 0;
#endif
}

bool write_private(const std::string &path, const std::string &text)
{
#ifndef _WIN32
    std::string name = path + ".tmp-XXXXXX";
    std::vector<char> temp(name.begin(), name.end());
    temp.push_back('\0');
    int fd = mkstemp(temp.data()); // O_EXCL and 0600; never follow a preexisting .part symlink.
    if (fd < 0)
        return false;
    std::size_t written = 0;
    bool ok = true;
    while (written < text.size())
    {
        const auto n = write(fd, text.data() + written, text.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        written += std::size_t(n);
    }
    ok = (fsync(fd) == 0) && ok;
    ok = (close(fd) == 0) && ok;
    if (ok) ok = rename(temp.data(), path.c_str()) == 0;
    if (!ok) unlink(temp.data());
    // PS5 filesystems may not implement directory fsync. The file itself was synced.
    if (ok)
    {
        const auto dir = path.substr(0, path.rfind('/'));
        const int d = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (d >= 0) { (void)fsync(d); close(d); }
    }
    return ok;
#else
    std::FILE *f = open_atomic(path);
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return finish_atomic(f, path) && ok;
#endif
}

std::string clean(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    const auto lead = s.find_first_not_of(" \t");
    return lead == std::string::npos ? "" : s.substr(lead);
}
std::uint32_t be32(const unsigned char *p)
{
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}
bool known_preferences(const Profile &profile, const Preferences &prefs)
{
    for (const auto &p : prefs)
    {
        bool found = false;
        for (const auto &code : profile.codes)
            if (p.first == ui::list_name("Gecko", code.cheat.name) && !code.required) found = true;
        if (!found) return false;
    }
    return true;
}
} // namespace

std::string config_folder(const std::string &user_root) { return user_root + "/Slippi"; }
std::string account_path(const std::string &user_root) { return config_folder(user_root) + "/user.json"; }

bool prepare_ftp_account_folder(const std::string &user_root, std::string &error)
{
    error.clear();
    struct stat st;
    if ((stat(user_root.c_str(), &st) != 0 && mkdir(user_root.c_str(), 0755) != 0) ||
        !private_folder(config_folder(user_root)))
        return fail(error, "Cannot prepare the Slippi FTP folder. Check storage permissions.");
    return true;
}

bool load_ftp_account(const std::string &user_root, Account &out, std::string &error)
{
    out = {}; error.clear();
    if (!prepare_ftp_account_folder(user_root, error)) return false;
    std::string text;
    Json normalized;
    Account summary;
    if (!read_bounded(account_path(user_root), kAccountLimit, text, true))
        return fail(error, "user.json is required for Slippi online. Upload it by FTP, then reload.");
    if (!parse_account(text, summary, normalized))
        return fail(error, "Invalid FTP account file. Upload a fresh official user.json, then reload.");
    if (!write_private(account_path(user_root), normalized.dump() + "\n"))
        return fail(error, "Could not secure the FTP account file. Check storage permissions and reload it.");
    out = summary;
    return true;
}

bool inspect_account(const std::string &user_root, Account &out, std::string &error)
{
    out = {}; error.clear();
    std::string text;
    Json normalized;
    if (!read_bounded(config_folder(user_root) + "/user.json", kAccountLimit, text))
        return fail(error, "No readable Slippi account file. Import the official user.json.");
    if (!parse_account(text, out, normalized))
        return fail(error, "Invalid Slippi account file. Download a fresh user.json from the official website.");
    return true;
}

bool import_account(const std::string &from, const std::string &user_root, Account &out, std::string &error)
{
    out = {}; error.clear();
    std::string text;
    Json normalized;
    Account summary;
    if (!read_bounded(from, kAccountLimit, text) || !parse_account(text, summary, normalized))
        return fail(error, "Invalid account export. Choose the official user.json (at most 16 KiB).");
    struct stat st;
    // Leave the existing offline User directory's permissions unchanged.
    if ((stat(user_root.c_str(), &st) != 0 && mkdir(user_root.c_str(), 0755) != 0) ||
        !private_folder(config_folder(user_root)))
        return fail(error, "Cannot create private Slippi storage. Check storage permissions.");
    if (!write_private(config_folder(user_root) + "/user.json", normalized.dump() + "\n"))
        return fail(error, "Could not save the account. The previously imported account was preserved.");
    out = summary;
    return true;
}

bool sign_out(const std::string &user_root, std::string &error)
{
    error.clear();
    // Do not follow a replaced config directory when deleting credentials.
#ifndef _WIN32
    struct stat st;
    const auto folder = config_folder(user_root);
    if (lstat(folder.c_str(), &st) == 0 && (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
        return fail(error, "Unsafe account folder. Sign out could not remove the imported account.");
#endif
    if (std::remove((config_folder(user_root) + "/user.json").c_str()) != 0 && errno != ENOENT)
        return fail(error, "Could not remove the imported account. Check storage permissions.");
    return true;
}

bool is_melee(const std::string &id) { return id == "GALE01" || id == "GALJ01"; }

bool inspect_disc(const std::string &path, Disc &out, std::string &error)
{
    out = {}; error.clear();
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return fail(error, "The disc image is missing or unreadable.");
    unsigned char h[0x440]{};
    const bool read = std::fread(h, 1, sizeof h, f) == sizeof h;
    struct stat st;
    const bool sized = fstat(fileno(f), &st) == 0 && S_ISREG(st.st_mode);
    std::fclose(f);
    if (!read || !sized) return fail(error, "The disc image header is incomplete.");
    if (be32(h + 0x1c) != 0xc2339f3d)
        return fail(error, "Slippi preparation currently requires an uncompressed ISO/GCM. Compressed images need backend verification.");
    out.id.assign(reinterpret_cast<char *>(h), 6);
    out.revision = h[7];
    if (!is_melee(out.id) || out.revision != 2 || h[6] != 0)
        return fail(error, "Select an NTSC-U or NTSC-J Super Smash Bros. Melee revision 1.02 disc.");
    const auto dol = be32(h + 0x420), fst = be32(h + 0x424), fst_size = be32(h + 0x428);
    if (dol < sizeof h || fst < sizeof h || fst_size < 12 ||
        std::uint64_t(dol) + 0x100 > std::uint64_t(st.st_size) ||
        std::uint64_t(fst) + fst_size > std::uint64_t(st.st_size))
        return fail(error, "The disc image is truncated or has invalid DOL/FST offsets.");
    out.header_compatible = true;
    return true;
}

bool load_profile(const std::string &asset_dir, const std::string &id, Profile &out, std::string &error)
{
    out = {}; error.clear();
    if (!is_melee(id)) return fail(error, "No official Slippi profile is bundled for this game.");
    const auto name = id + "r2.ini";
    std::string text;
    if (!read_bounded(asset_dir + "/" + name, kCodeLimit, text) ||
        sha256(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()) != kDigests.at(name))
        return fail(error, "Official Slippi code integrity check failed. Reinstall Porpoise's Slippi assets.");
    Profile candidate;
    candidate.id = id; candidate.source = text;
    for (const auto &cheat : ui::cheats_from_ini(text))
    {
        if (cheat.kind != "Gecko") continue;
        Code code; code.cheat = cheat;
        const auto list = ui::list_name("Gecko", cheat.name);
        code.required = list.compare(0, 10, "$Required:") == 0;
        candidate.codes.push_back(code);
    }
    for (const auto required : kRequired)
    {
        bool found = false;
        for (const auto &c : candidate.codes)
            if (ui::list_name("Gecko", c.cheat.name) == required) found = true;
        if (!found) return fail(error, "Official Slippi profile is missing a mandatory code.");
    }
    // Attach the descriptions already carried by Dolphin's official INI syntax.
    std::istringstream stream(text);
    std::string line, section, current;
    while (std::getline(stream, line))
    {
        line = clean(line);
        if (!line.empty() && line.front() == '[') { section = line; current.clear(); }
        if (section != "[Gecko]" || line.empty()) continue;
        if (line.front() == '$') current = ui::list_name("Gecko", line);
        else if (line.front() == '*')
            for (auto &c : candidate.codes)
                if (ui::list_name("Gecko", c.cheat.name) == current)
                    c.description += (c.description.empty() ? "" : " ") + line.substr(1);
    }
    out = std::move(candidate);
    return true;
}

bool enabled(const Code &code, const Preferences &preferences)
{
    if (code.required) return true;
    const auto it = preferences.find(ui::list_name("Gecko", code.cheat.name));
    return it == preferences.end() ? code.cheat.default_on : it->second;
}

bool load_preferences(const std::string &path, Preferences &out, std::string &error)
{
    out.clear(); error.clear();
    struct stat st;
    if (stat(path.c_str(), &st) != 0 && errno == ENOENT) return true;
    std::string text; Json j;
    if (!read_bounded(path, kAccountLimit, text) || !parse_json(text, j))
        return fail(error, "Slippi code preferences are invalid. No preferences were applied.");
    for (auto it = j.begin(); it != j.end(); ++it)
        if (!it.value().is_boolean()) return fail(error, "Slippi code preferences must contain only switches.");
    for (auto it = j.begin(); it != j.end(); ++it) out[it.key()] = it.value().get<bool>();
    return true;
}

bool save_preferences(const std::string &path, const Preferences &prefs, std::string &error)
{
    error.clear();
    if (!write_private(path, Json(prefs).dump(2) + "\n"))
        return fail(error, "Could not save Slippi code preferences.");
    return true;
}

bool write_profile(const Profile &profile, const Preferences &prefs, const std::string &path, std::string &error)
{
    error.clear();
    const auto digest = kDigests.find(profile.id + "r2.ini");
    if (digest == kDigests.end() ||
        sha256(reinterpret_cast<const std::uint8_t *>(profile.source.data()), profile.source.size()) != digest->second)
        return fail(error, "Slippi profile changed after verification. Refusing to prepare codes.");
    // Re-read the code metadata from the verified source, rather than trusting
    // a caller-modified Code vector when deciding which names must be enabled.
    std::set<std::string> required(kRequired, kRequired + 3);
    if (!known_preferences(profile, prefs))
        return fail(error, "Slippi preferences contain a required or unknown code. Offline cheats are not allowed.");
    std::string output;
    std::istringstream in(profile.source);
    std::string line;
    bool skip = false;
    while (std::getline(in, line))
    {
        const auto s = clean(line);
        if (!s.empty() && s.front() == '[') skip = s == "[Gecko_Enabled]";
        if (!skip) output += line + "\n";
    }
    output += "\n[Gecko_Enabled]\n";
    for (const auto &name : required) output += name + "\n";
    // Select optional code names exclusively from the official text.
    in.clear(); in.str(profile.source);
    bool gecko = false;
    std::set<std::string> default_on;
    bool defaults = false;
    while (std::getline(in, line))
    {
        line = clean(line);
        if (!line.empty() && line.front() == '[') defaults = line == "[Gecko_Enabled]";
        else if (defaults && !line.empty() && line.front() == '$') default_on.insert(line);
    }
    in.clear(); in.str(profile.source);
    while (std::getline(in, line))
    {
        line = clean(line);
        if (!line.empty() && line.front() == '[') gecko = line == "[Gecko]";
        else if (gecko && !line.empty() && line.front() == '$')
        {
            const auto name = ui::list_name("Gecko", line);
            if (required.count(name)) continue;
            const auto p = prefs.find(name);
            if (p == prefs.end() ? default_on.count(name) != 0 : p->second) output += name + "\n";
        }
    }
    if (!write_private(path, output)) return fail(error, "Could not write the isolated Slippi profile.");
    return true;
}
} // namespace porpoise::slippi
