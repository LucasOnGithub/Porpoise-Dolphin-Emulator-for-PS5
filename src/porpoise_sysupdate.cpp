/* Porpoise - Wii System Update's list of titles (see the header).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_sysupdate.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "porpoise_http.hpp"
#include "trace.hpp"

namespace porpoise::sysupdate
{
namespace
{
std::mutex g_list_lock; /* the list's rename against cancel() */
}

void cancel(Job &job)
{
    std::lock_guard<std::mutex> lock(g_list_lock);
    job.cancelled.store(true);
}

namespace
{
/* Dolphin's own NetUpdateSOAP endpoint (Nintendo's own for the Wii is
 * offline); the titles themselves come from Nintendo's servers. */
constexpr const char *kUrl = "https://fakenus.dolphin-emu.org/nus/services/NetUpdateSOAP";

void fail(Job &job, const char *why)
{
    std::lock_guard<std::mutex> lock(job.m);
    job.error = why;
    job.state.store(2);
}
} // namespace

void fetch(const std::string &region, const std::string &out_path, Job &job)
{
    /* As Dolphin's request (WiiUtils.cpp): the service doesn't check the
     * device, so Dolphin's default console's ID serves. */
    const std::string body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<soapenv:Envelope xmlns:soapenv=\"http://schemas.xmlsoap.org/soap/envelope/\"\n"
        "  xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"\n"
        "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n"
        "  <soapenv:Body>\n"
        "    <GetSystemUpdateRequest xmlns=\"urn:nus.wsapi.broadon.com\">\n"
        "      <Version>1.0</Version>\n"
        "      <MessageId>0</MessageId>\n"
        "      <DeviceId>4362316904</DeviceId>\n"
        "      <RegionId>" +
        region +
        "</RegionId>\n"
        "    </GetSystemUpdateRequest>\n"
        "  </soapenv:Body>\n"
        "</soapenv:Envelope>\n";
    const std::vector<std::pair<std::string, std::string>> headers = {
        {"SOAPAction", "urn:nus.wsapi.broadon.com/GetSystemUpdate"},
        {"Content-Type", "text/xml; charset=utf-8"},
    };
    porpoise::http::Session http("porpoise-sysupdate", "wii libnup/1.0");
    if (!http.init())
    {
        fail(job, "The console isn't online.");
        return;
    }
    std::vector<std::uint8_t> out;
    const int status = http.request(kUrl, &body, out, std::size_t(1) << 20, &headers);
    http.term();
    const std::string text(out.begin(), out.end());
    char line[96];
    std::snprintf(line, sizeof line, "sysupdate: HTTP %d, %zu bytes", status, out.size());
    ps5::debug::mark(line);
    if (status != 200 || text.find("GetSystemUpdateResponse") == std::string::npos ||
        text.find("ContentPrefixURL") == std::string::npos)
    {
        fail(job, "The update service didn't answer. Try again in a while.");
        return;
    }
    /* Written aside and renamed into place: a worker left behind by a
     * cancelled request never leaves a half list for the next one. */
    if (job.cancelled.load())
        return;
    char tag[32];
    std::snprintf(tag, sizeof tag, ".%p.part", static_cast<void *>(&job)); /* one aside per request */
    const std::string part = out_path + tag;
    std::FILE *f = std::fopen(part.c_str(), "wb");
    const bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (job.cancelled.load())
    {
        if (f)
            std::fclose(f);
        std::remove(part.c_str());
        return;
    }
    bool renamed = false;
    if (f && std::fclose(f) == 0 && ok)
    {
        /* With cancel(): a cancelled job's list never lands after it. */
        std::lock_guard<std::mutex> lock(g_list_lock);
        if (job.cancelled.load())
        {
            std::remove(part.c_str());
            return;
        }
        renamed = std::rename(part.c_str(), out_path.c_str()) == 0;
    }
    if (!renamed)
    {
        std::remove(part.c_str());
        fail(job, "The list of updates couldn't be saved: is the drive full?");
        return;
    }
    job.state.store(1);
}
} // namespace porpoise::sysupdate
