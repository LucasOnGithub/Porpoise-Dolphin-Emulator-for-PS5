/* Host checks of the console boundary, including unavailable services.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runtime.h"
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *mode;
static unsigned random_calls, max_chunk, fail_random_call, nbio_calls, closes;
static int fail_nbio;
#ifdef SLIPPI_TEST_ENET
void test_slippi_ps5_enet(void);
#endif
static int real_random(uint8_t *out, size_t size)
{
    ++random_calls;
    if (size > max_chunk) max_chunk = (unsigned)size;
    if (random_calls == fail_random_call) return -1;
    return getrandom(out, size, 0) == (ssize_t)size ? 0 : -1;
}
static int net_init(void) { return 0; }
void *__wrap_dlopen(const char *name, int flags)
{
    (void)flags;
    if (strcmp(name, "libSceRandom.sprx") == 0)
        return strcmp(mode, "no-random") == 0 ? NULL : (void *)1;
    if (strcmp(name, "libSceNet.sprx") == 0)
        return strcmp(mode, "no-net") == 0 ? NULL : (void *)2;
    return NULL;
}
void *__wrap_dlsym(void *module, const char *name)
{
    if (module == (void *)1 && !strcmp(name, "sceRandomGetRandomNumber")) return (void *)real_random;
    if (module == (void *)2 && !strcmp(name, "sceNetInit")) return (void *)net_init;
    return NULL;
}
int __real_setsockopt(int, int, int, const void *, socklen_t);
int __wrap_setsockopt(int fd, int level, int option, const void *value, socklen_t size)
{
    if (level != SOL_SOCKET || option != 0x1200)
        return __real_setsockopt(fd, level, option, value, size);
    ++nbio_calls;
    if (fail_nbio) { errno = EIO; return -1; }
    assert(value && size == sizeof(int));
    return 0; /* ioctl above it changes the actual host socket's mode. */
}
int __real_close(int);
int __wrap_close(int fd) { ++closes; return __real_close(fd); }

int main(int argc, char **argv)
{
    mode = argc > 1 ? argv[1] : "normal";
    unsigned char bytes[129];
    if (strcmp(mode, "normal")) {
        assert(slippi_ps5_platform_init() == -1);
        assert(slippi_ps5_getrandom(bytes, sizeof bytes, 0) == -1);
        assert(slippi_ps5_socket(AF_INET, SOCK_DGRAM, 0) == -1);
        assert(random_calls == 0);
        printf("Unavailable service (%s) correctly refused\n", mode);
        return 0;
    }
    assert(slippi_ps5_platform_init() == 0);
    assert(slippi_ps5_getrandom(NULL, 0, 0) == 0);
    assert(slippi_ps5_getrandom(NULL, 1, 0) == -1 && errno == EINVAL);
    assert(slippi_ps5_getrandom(bytes, 1, 4) == -1 && errno == EINVAL);
    assert(slippi_ps5_getrandom(bytes, sizeof bytes, 0) == sizeof bytes);
    assert(random_calls == 3 && max_chunk == 64);
    fail_random_call = random_calls + 2;
    memset(bytes, 0xff, sizeof bytes);
    assert(slippi_ps5_getrandom(bytes, sizeof bytes, 0) == -1 && errno == EIO);
    for (size_t i = 0; i < sizeof bytes; ++i) assert(bytes[i] == 0);
    fail_random_call = 0;
    slippi_ps5_arc4random_buf(bytes, sizeof bytes);
    int fd = slippi_ps5_socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    assert(fd >= 0 && (fcntl(fd, F_GETFD) & FD_CLOEXEC));
    assert(nbio_calls == 1);
    assert(recv(fd, bytes, sizeof bytes, 0) == -1 && errno == EAGAIN);
    int zero = 0;
    assert(slippi_ps5_ioctl(fd, FIONBIO, &zero) == 0 && !(fcntl(fd, F_GETFL) & O_NONBLOCK));
    assert(slippi_ps5_ioctl(fd, FIONBIO, NULL) == -1 && errno == EINVAL);
    close(fd);
    unsigned old_closes = closes;
    fail_nbio = 1;
    assert(slippi_ps5_socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0) == -1 && errno == EIO);
    assert(closes == old_closes + 1);
#ifdef SLIPPI_TEST_ENET
    fail_nbio = 0;
    test_slippi_ps5_enet();
#endif
    puts("PS5 boundary checks passed: entropy, failure handling, socket flags and cleanup");
    return 0;
}
