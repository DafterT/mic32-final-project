#ifndef APP_BUZZER_BUZZER_H
#define APP_BUZZER_BUZZER_H

#include "app_controller.h"

#include <stdint.h>

void buzzer_init(void);
void buzzer_play(AppSound sound, uint32_t now_ms);
void buzzer_update(uint32_t now_ms);
void buzzer_stop(void);

#endif
