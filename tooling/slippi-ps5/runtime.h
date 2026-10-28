/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
int slippi_ps5_platform_init(void);
ssize_t slippi_ps5_getrandom(void *buffer, size_t size, unsigned flags);
void slippi_ps5_arc4random_buf(void *buffer, size_t size);
int slippi_ps5_socket(int domain, int type, int protocol);
int slippi_ps5_ioctl(int fd, unsigned long request, ...);
int slippi_ps5_validate_abi(const uint64_t *values, size_t count);
#ifdef __cplusplus
}
#endif
