#ifndef APP_H_
#define APP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define APP_FW_VERSION   0x0100
#define MODBUS_UNIT_ID   1

struct app_controller;
struct app_controller *app_init(void);
void app_set_led(struct app_controller *app, bool on);
bool app_get_led(struct app_controller *app);
void app_request_reboot(struct app_controller *app);
bool app_reboot_requested(struct app_controller *app);
void app_set_button(struct app_controller *app, bool pressed);
bool app_get_button(struct app_controller *app);
uint32_t app_get_press_count(struct app_controller *app);
void app_set_press_count(struct app_controller *app, uint32_t count);
void app_load_config(struct app_controller *app, uint16_t led_default);
void app_read_config(struct app_controller *app, uint16_t *led_default);
void app_set_led_default(struct app_controller *app, uint16_t led_default);
uint16_t app_get_led_default(struct app_controller *app);
bool app_consume_config_dirty(struct app_controller *app);
void app_set_rtc_epoch(struct app_controller *app, uint32_t epoch);
uint32_t app_get_rtc_epoch(struct app_controller *app);
void app_set_time_hi(struct app_controller *app, uint16_t hi);
void app_request_time_set(struct app_controller *app, uint16_t lo);
uint32_t app_get_set_epoch(struct app_controller *app);
bool app_consume_time_set(struct app_controller *app, uint32_t *epoch);

#ifdef __cplusplus
}
#endif

#endif /* APP_H_ */
