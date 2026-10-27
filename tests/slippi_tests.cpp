// Host-only tests; all credentials and images below are synthetic.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "porpoise_slippi.hpp"
#include "porpoise_sha256.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace porpoise::slippi;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "Failed at line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
static void put(const fs::path &p, const std::string &s) { std::ofstream(p, std::ios::binary) << s; }
static std::string get(const fs::path &p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void word(std::string &s, unsigned at, unsigned value)
{
    for (unsigned i = 0; i < 4; ++i) s[at + i] = char(value >> (24 - 8 * i));
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    char folder[] = "/tmp/porpoise-slippi-XXXXXX";
    CHECK(mkdtemp(folder));
    const fs::path dir = folder, user = dir / "User", source = dir / "user.json";
    fs::create_directory(user);
    chmod(user.c_str(), 0755);
    std::string error;
    Account account;
    CHECK(!inspect_account(user, account, error));
    CHECK(!account.imported);
    const std::string valid = R"({"uid":"synthetic-test-uid","playKey":"synthetic-test-secret","displayName":"Lucas テスト","connectCode":"TEST#123","latestVersion":"3.0.0","ignored":"discard me"})";
    put(source, valid);
    CHECK(import_account(source, user, account, error));
    CHECK(account.imported && account.display_name == "Lucas テスト" && account.connect_code == "TEST#123");
    CHECK(error.empty());
    CHECK(get(source) == valid);
    const auto stored = fs::path(config_folder(user)) / "user.json";
    const auto original = get(stored);
    CHECK(original.find("ignored") == std::string::npos);
    struct stat st;
    CHECK(stat(stored.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(stat(stored.parent_path().c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    CHECK(stat(user.c_str(), &st) == 0 && (st.st_mode & 0777) == 0755);
    CHECK(inspect_account(user, account, error));
    const std::vector<std::string> invalid = {
        "", "{}", "[]", "not json", valid + "trailing", "{\"uid\":null}",
        std::string(16385, 'x'),
        "{\"uid\":\"a\",\"uid\":\"b\",\"playKey\":\"secret\",\"displayName\":\"X\",\"connectCode\":\"T#123\"}",
        "{\"uid\":\"a\",\"playKey\":42,\"displayName\":\"X\",\"connectCode\":\"T#123\"}",
        "{\"uid\":\"a\",\"playKey\":\"secret\",\"displayName\":\"\\u0000X\",\"connectCode\":\"T#123\"}",
        "{\"uid\":\"a\",\"playKey\":\"secret\",\"displayName\":\"\\ud800\",\"connectCode\":\"T#123\"}",
        "{\"uid\":\"a\",\"playKey\":\"secret\",\"displayName\":\"X\",\"connectCode\":\"T#123\",\"latestVersion\":false}",
        "{\"nested\":" + std::string(100, '[') + "0" + std::string(100, ']') + "}",
    };
    for (const auto &bad : invalid)
    {
        put(source, bad);
        CHECK(!import_account(source, user, account, error));
        CHECK(!account.imported);
        CHECK(error.find("secret") == std::string::npos && error.find("synthetic") == std::string::npos);
        CHECK(get(stored) == original);
    }
    put(source, valid);
    fs::create_symlink(source, dir / "link.json");
    CHECK(!import_account(dir / "link.json", user, account, error));
    CHECK(mkfifo((dir / "fifo.json").c_str(), 0600) == 0);
    CHECK(!import_account(dir / "fifo.json", user, account, error));
    fs::create_directory(dir / "unsafe");
    fs::create_directory_symlink(stored.parent_path(), dir / "unsafe/Slippi");
    CHECK(!import_account(source, dir / "unsafe", account, error));
    CHECK(!sign_out(dir / "unsafe", error));
    CHECK(get(stored) == original);
    CHECK(sign_out(user, error));
    CHECK(sign_out(user, error));
    CHECK(!inspect_account(user, account, error));
    CHECK(!account.imported);

    // FTP writes directly to the backend's canonical path: no browser import.
    const fs::path ftp_user = dir / "FtpUser", ftp_file = account_path(ftp_user);
    CHECK(ftp_file == ftp_user / "Slippi/user.json");
    CHECK(prepare_ftp_account_folder(ftp_user, error));
    CHECK(fs::is_directory(ftp_file.parent_path()));
    CHECK(!load_ftp_account(ftp_user, account, error) && !account.imported);
    CHECK(error.find("FTP") != std::string::npos);
    put(ftp_file, valid);
    chmod(ftp_file.c_str(), 0666); // A typical externally uploaded file.
    chmod(ftp_file.parent_path().c_str(), 0777);
    CHECK(load_ftp_account(ftp_user, account, error));
    CHECK(account.imported && account.connect_code == "TEST#123");
    CHECK(stat(ftp_file.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(stat(ftp_file.parent_path().c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    CHECK(get(ftp_file).find("ignored") == std::string::npos);
    CHECK(load_ftp_account(ftp_user, account, error));
    auto replacement = valid;
    replacement.replace(replacement.find("TEST#123"), 8, "NEXT#456");
    put(ftp_file, replacement);
    CHECK(load_ftp_account(ftp_user, account, error) && account.connect_code == "NEXT#456");
    put(ftp_file, "{partial FTP upload");
    CHECK(!load_ftp_account(ftp_user, account, error));
    CHECK(!account.imported && account.display_name.empty() && account.connect_code.empty());
    CHECK(error.find("synthetic-test-secret") == std::string::npos);
    fs::remove(ftp_file);
    CHECK(!load_ftp_account(ftp_user, account, error) && !account.imported);
    fs::create_symlink(source, ftp_file);
    CHECK(!load_ftp_account(ftp_user, account, error) && !account.imported);
    fs::remove(ftp_file);
    CHECK(mkfifo(ftp_file.c_str(), 0600) == 0);
    CHECK(!load_ftp_account(ftp_user, account, error) && !account.imported);
    fs::remove(ftp_file);
    CHECK(!load_ftp_account(dir / "unsafe", account, error));
    CHECK(!account.imported);
    put(ftp_file, valid);
    CHECK(load_ftp_account(ftp_user, account, error));
    CHECK(sign_out(ftp_user, error));
    CHECK(!load_ftp_account(ftp_user, account, error) && !account.imported);

    CHECK(porpoise::sha256(reinterpret_cast<const std::uint8_t *>(""), 0) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(porpoise::sha256(reinterpret_cast<const std::uint8_t *>("abc"), 3) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string million(1000000, 'a');
    CHECK(porpoise::sha256(reinterpret_cast<const std::uint8_t *>(million.data()), million.size()) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    for (const std::string id : {"GALE01", "GALJ01"})
    {
        Profile profile;
        CHECK(load_profile(argv[1], id, profile, error));
        CHECK(profile.codes.size() == 12);
        unsigned mandatory = 0, defaults = 0;
        Preferences prefs;
        for (const auto &c : profile.codes)
        {
            mandatory += c.required;
            defaults += enabled(c, prefs);
            if (c.required) CHECK(enabled(c, {{porpoise::ui::list_name("Gecko", c.cheat.name), false}}));
        }
        CHECK(mandatory == 3 && defaults == 6);
        CHECK(load_preferences(dir / "absent.json", prefs, error) && prefs.empty());
        const auto optional = porpoise::ui::list_name("Gecko", profile.codes.back().cheat.name);
        prefs[optional] = true;
        CHECK(save_preferences(dir / "prefs.json", prefs, error));
        Preferences read;
        CHECK(load_preferences(dir / "prefs.json", read, error) && read == prefs);
        CHECK(write_profile(profile, prefs, dir / "online.ini", error));
        auto codes = porpoise::ui::cheats_from_ini(get(dir / "online.ini"));
        CHECK(codes.size() == 12);
        unsigned on = 0;
        for (const auto &c : codes) on += c.default_on;
        CHECK(on == 7);
        CHECK(get(dir / "online.ini").find("CPUThread = True") != std::string::npos);
        CHECK(!write_profile(profile, {{"$Required: Slippi Online", false}}, dir / "online.ini", error));
        CHECK(!write_profile(profile, {{"$Infinite Lives", true}}, dir / "online.ini", error));
        auto corrupt = profile;
        corrupt.source += "\n# modified\n";
        CHECK(!write_profile(corrupt, prefs, dir / "online.ini", error));
        fs::create_directories(dir / "assets");
        put(dir / "assets" / (id + "r2.ini"), profile.source + "\n");
        CHECK(!load_profile(dir / "assets", id, corrupt, error));
        CHECK(corrupt.codes.empty());
    }
    put(dir / "bad-prefs.json", "{\"bad\":42}");
    Preferences prefs;
    CHECK(!load_preferences(dir / "bad-prefs.json", prefs, error) && prefs.empty());
    Profile profile;
    CHECK(!load_profile(argv[1], "GALP01", profile, error));
    CHECK(!is_melee("GALP01") && is_melee("GALE01") && is_melee("GALJ01"));

    std::string disc(0x2000, '\0');
    disc.replace(0, 6, "GALE01"); disc[7] = 2;
    word(disc, 0x1c, 0xc2339f3d); word(disc, 0x420, 0x500);
    word(disc, 0x424, 0x1000); word(disc, 0x428, 12);
    Disc result;
    put(dir / "random-name.iso", disc);
    CHECK(inspect_disc(dir / "random-name.iso", result, error));
    CHECK(result.id == "GALE01" && result.revision == 2 && result.header_compatible);
    disc.replace(0, 6, "GALJ01"); put(dir / "j.iso", disc);
    CHECK(inspect_disc(dir / "j.iso", result, error));
    disc[7] = 1; put(dir / "revision1.iso", disc);
    CHECK(!inspect_disc(dir / "revision1.iso", result, error));
    disc[7] = 2; disc[6] = 1; put(dir / "disc2.iso", disc);
    CHECK(!inspect_disc(dir / "disc2.iso", result, error));
    disc[6] = 0; disc.replace(0, 6, "OTHER1"); put(dir / "Super Smash Bros Melee 1.02.iso", disc);
    CHECK(!inspect_disc(dir / "Super Smash Bros Melee 1.02.iso", result, error));
    disc.replace(0, 6, "GALE01"); disc.resize(0x600); put(dir / "truncated.iso", disc);
    CHECK(!inspect_disc(dir / "truncated.iso", result, error));
    CHECK(!inspect_disc(dir / "missing.iso", result, error));
    put(dir / "compressed.rvz", "RVZ\x01" "fake");
    CHECK(!inspect_disc(dir / "compressed.rvz", result, error));
    for (const auto &p : fs::recursive_directory_iterator(dir))
        CHECK(p.path().string().find(".tmp-") == std::string::npos);
    fs::remove_all(dir);
    std::cout << checks << " Slippi checks passed (synthetic data; no network or PS5 validation).\n";
}
