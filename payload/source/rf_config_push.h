#ifndef RF_CONFIG_PUSH_H
#define RF_CONFIG_PUSH_H

#include "rf_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void *config_push_worker(void *arg);
bool device_screen_button_has_cmd(uint8_t slot, const char *dev_id, const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* RF_CONFIG_PUSH_H */
