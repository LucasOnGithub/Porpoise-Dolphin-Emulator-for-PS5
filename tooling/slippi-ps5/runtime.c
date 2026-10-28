/* PS5 bindings for the matched Slippi Rust backend.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Rust's std is rebuilt with pthread TLS and the SDK-compatible FreeBSD 10
 * libc layout. Only this archive's imports are renamed to these functions.
 * Never route TLS entropy to the platform's timestamp-seeded arc4random shim.
 */
#include "runtime.h"
#include <dlfcn.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#ifndef PORPOISE_SLIPPI_PLATFORM_HOST_TEST
#include <pthread_np.h>
#endif
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef PORPOISE_SLIPPI_PLATFORM_HOST_TEST
const uint64_t SLIPPI_PS5_C_ABI[] = {
    /* Compared before any Rust filesystem or network operation. This catches
     * accidental use of modern FreeBSD stat/dirent layouts in std or crates. */

        sizeof(struct stat), offsetof(struct stat, st_size),
        offsetof(struct stat, st_mtim), offsetof(struct stat, st_birthtim),
        sizeof(struct dirent), offsetof(struct dirent, d_name),
        sizeof(struct addrinfo), offsetof(struct addrinfo, ai_addr),
        offsetof(struct addrinfo, ai_canonname), offsetof(struct addrinfo, ai_next),
        sizeof(struct sockaddr_in), sizeof(struct sockaddr_in6),
        sizeof(struct sockaddr_storage), sizeof(socklen_t),
        sizeof(struct timespec), sizeof(struct timeval),
        sizeof(pthread_t), sizeof(pthread_key_t), sizeof(pthread_mutex_t), sizeof(pthread_cond_t),
        sizeof(pthread_attr_t), CLOCK_MONOTONIC, AF_INET, SOL_SOCKET, SO_ERROR,
        SO_RCVTIMEO, SO_SNDTIMEO, SO_NOSIGPIPE, FIONBIO, SOCK_CLOEXEC, SOCK_NONBLOCK,
        O_NONBLOCK, EAGAIN, EINPROGRESS, ETIMEDOUT, EINTR,
    };
int slippi_ps5_validate_abi(const uint64_t *values, size_t count)
{
    const uint64_t *expected = SLIPPI_PS5_C_ABI;
    if (!values || count != sizeof SLIPPI_PS5_C_ABI / sizeof SLIPPI_PS5_C_ABI[0])
        return -1;
    for (size_t i = 0; i < count; ++i)
        if (values[i] != expected[i])
            return (int)i + 1;
    return 0;
}
#else
int slippi_ps5_validate_abi(const uint64_t *values, size_t count)
{
    (void)values; (void)count;
    return -1; /* Only the actual SDK can validate the console ABI. */
}
#endif

static pthread_once_t platform_once = PTHREAD_ONCE_INIT;
static int (*console_random)(uint8_t *, size_t);
static int network_ready;
/* Keep modules loaded while Rust workers may call them. */
static void *random_module, *net_module;

int slippi_ps5_pthread_setname_np(pthread_t thread, const char *name)
{
#ifdef PORPOISE_SLIPPI_PLATFORM_HOST_TEST
    return pthread_setname_np(thread, name);
#else
    /* Rust targets FreeBSD 12.2's spelling; the SDK has the older void API. */
    pthread_set_name_np(thread, name);
    return 0;
#endif
}

static void initialize(void)
{
    random_module = dlopen("libSceRandom.sprx", RTLD_LAZY);
    if (random_module)
        console_random = (int (*)(uint8_t *, size_t))dlsym(random_module, "sceRandomGetRandomNumber");
    net_module = dlopen("libSceNet.sprx", RTLD_LAZY);
    if (net_module) {
        int (*net_init)(void) = (int (*)(void))dlsym(net_module, "sceNetInit");
        if (net_init) {
            /* Like the existing HTTP/DNS code: it may already be initialized.
             * Subsequent socket calls still report their actual errors. */
            (void)net_init();
            network_ready = 1;
        }
    }
}

int slippi_ps5_platform_init(void)
{
    int rc = pthread_once(&platform_once, initialize);
    if (rc || !console_random || !network_ready) {
        errno = rc ? rc : ENOSYS;
        return -1;
    }
    return 0;
}

ssize_t slippi_ps5_getrandom(void *buffer, size_t size, unsigned flags)
{
    /* FreeBSD GRND_NONBLOCK and GRND_RANDOM do not alter this service's pool. */
    if (flags & ~3u || size > (size_t)SSIZE_MAX || (!buffer && size)) {
        errno = EINVAL;
        return -1;
    }
    if (!size)
        return 0;
    if (slippi_ps5_platform_init() != 0)
        return -1;
    uint8_t *out = buffer;
    for (size_t done = 0; done < size;) {
        /* The SDK's random sample uses 64-byte requests. */
        size_t chunk = size - done < 64 ? size - done : 64;
        if (console_random(out + done, chunk) != 0) {
            memset(buffer, 0, size);
            errno = EIO;
            return -1;
        }
        done += chunk;
    }
    return (ssize_t)size;
}

void slippi_ps5_arc4random_buf(void *buffer, size_t size)
{
    /* std's HashMap/STUN RNG has no error return. Stop rather than supply
     * predictable bytes if the real console service is unavailable. */
    if (slippi_ps5_getrandom(buffer, size, 0) < 0)
        abort();
}

int slippi_ps5_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    va_start(args, request);
    void *argument = va_arg(args, void *);
    va_end(args);
    if (request != FIONBIO)
        return ioctl(fd, request, argument);
    if (!argument) {
        errno = EINVAL;
        return -1;
    }
    /* Porpoise's SMB probe establishes the console's additional SO_NBIO
     * requirement. fcntl(O_NONBLOCK) alone is insufficient on this network. */
    (void)ioctl(fd, request, argument);
    return setsockopt(fd, SOL_SOCKET, 0x1200, argument, sizeof(int));
}

int slippi_ps5_socket(int domain, int type, int protocol)
{
    if (slippi_ps5_platform_init() != 0)
        return -1;
    int fd = socket(domain, type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK), protocol);
    if (fd < 0)
        return -1;
    int flags = 0;
    if (type & SOCK_CLOEXEC) {
        flags = fcntl(fd, F_GETFD);
        if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
            goto failed;
    }
    if (type & SOCK_NONBLOCK) {
        int one = 1;
        if (slippi_ps5_ioctl(fd, FIONBIO, &one) < 0)
            goto failed;
    }
    return fd;
failed:
    flags = errno;
    close(fd);
    errno = flags;
    return -1;
}
