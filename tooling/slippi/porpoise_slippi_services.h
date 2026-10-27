/* Native C boundary for tooling/slippi. Not exported by dolphin_libretro.so.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Services PpSlippiServices;
PpSlippiServices *pp_slippi_services_create(const char *folder, const char *version);
bool pp_slippi_account_load(const PpSlippiServices *services); /* loaded != authenticated */
void pp_slippi_account_logout(PpSlippiServices *services);
uint64_t pp_slippi_search_begin(const PpSlippiServices *services, uint8_t mode, uint16_t enet_port,
                                const uint8_t *shift_jis_code, size_t length);
int32_t pp_slippi_search_state(const PpSlippiServices *services);
void pp_slippi_search_cancel(const PpSlippiServices *services, uint64_t session);
char *pp_slippi_search_take_result(const PpSlippiServices *services); /* private backend payload, never log */
void pp_slippi_string_free(char *text);
void pp_slippi_services_destroy(PpSlippiServices *services);
#ifdef __cplusplus
}
#endif
