#ifndef SETUP_SETUP_HW_H_
#define SETUP_SETUP_HW_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct app_controller;

int setup_init(void);
struct app_controller *setup_app(void);
void setup_config_save(struct app_controller *app);
int setup_config_read_saved(uint16_t *led_default, uint32_t *press_count);

#ifdef __cplusplus
}
#endif

#endif /* SETUP_SETUP_HW_H_ */
