/* PS5 ELF-loader diagnostic. Not an emulator or an online-play release.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "runtime.h"
#include <stdio.h>
#include <sys/stat.h>

extern unsigned slprs_ps5_platform_probe(void (*report)(unsigned, int));
static FILE *log_file;
static void report(unsigned stage, int result)
{
    static const char *names[] = {"initialization", "C/Rust ABI", "secure RNG",
        "std/threads/TLS destructors/time", "filesystem", "UDP/nonblocking",
        "DNS", "HTTPS certificate verification", "TLS WebSocket handshake"};
    const char *name = stage < sizeof names / sizeof names[0] ? names[stage] : "unknown";
    printf("Slippi probe %u: %s: %s (%d)\n", stage, name, result == 0 ? "PASS" : "FAIL", result);
    if (log_file) {
        fprintf(log_file, "%u %s: %s (%d)\n", stage, name, result == 0 ? "PASS" : "FAIL", result);
        fflush(log_file);
    }
}
int main(void)
{
    mkdir("/data/porpoise", 0777);
    log_file = fopen("/data/porpoise/slippi-platform-probe.txt", "w");
    if (log_file) {
        fputs("Experimental Slippi PS5 platform probe; no account or matchmaking join.\n", log_file);
        fflush(log_file);
    }
    int initialized = slippi_ps5_platform_init();
    report(0, initialized);
    unsigned failed = initialized == 0 ? slprs_ps5_platform_probe(report) : 1;
    if (log_file) {
        fprintf(log_file, "Finished: %u failed stages. This does not test a match.\n", failed);
        fclose(log_file);
    }
    return failed ? 1 : 0;
}
