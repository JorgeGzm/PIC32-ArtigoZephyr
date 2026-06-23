#ifndef DISPLAY_H_
#define DISPLAY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct app_controller;

int ui_init(struct app_controller *app);
void ui_boot_splash(void);
void ui_render(void);
void ui_update_leds(void);
void ui_service_keys(void);
void ui_notify(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H_ */
