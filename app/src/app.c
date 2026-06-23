#include "app.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_ctrl, LOG_LEVEL_INF);

struct app_controller {
	struct k_mutex lock;
	bool led_on;
	bool reboot_req;
	bool button_state;
	bool config_dirty;        /* set by Modbus, flushed to NVS by main loop */
	bool time_set_req;        /* set by Modbus, applied by main loop */
	uint16_t set_epoch_hi;    /* buffered high word for a time-set write */
	uint16_t led_default;     /* 0/1 */
	uint32_t press_count;
	uint32_t rtc_epoch;       /* cached RTC time (epoch seconds) */
	uint32_t set_epoch;       /* full epoch to apply (set by Modbus) */
};

static inline void app_lock(struct app_controller *app);
static inline void app_unlock(struct app_controller *app);
static void led_drive(struct app_controller *app);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static struct app_controller app_instance;

static inline void app_lock(struct app_controller *app)
{
	k_mutex_lock(&app->lock, K_FOREVER);
}

static inline void app_unlock(struct app_controller *app)
{
	k_mutex_unlock(&app->lock);
}

/* Drive led0 from app->led_on. Only called with the lock held. */
static void led_drive(struct app_controller *app)
{
	gpio_pin_set_dt(&led, app->led_on ? 1 : 0);
}

struct app_controller *app_init(void)
{
	struct app_controller *app = &app_instance;

	k_mutex_init(&app->lock);

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED GPIO not ready");
		return NULL;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	return app;
}

void app_set_led(struct app_controller *app, bool on)
{
	app_lock(app);
	app->led_on = on;
	led_drive(app);
	app_unlock(app);
}

bool app_get_led(struct app_controller *app)
{
	bool on = false;

	app_lock(app);
	on = app->led_on;
	app_unlock(app);
	return on;
}

void app_request_reboot(struct app_controller *app)
{
	app_lock(app);
	app->reboot_req = true;
	app_unlock(app);
}

bool app_reboot_requested(struct app_controller *app)
{
	bool req = false;

	app_lock(app);
	req = app->reboot_req;
	app_unlock(app);
	return req;
}

void app_set_button(struct app_controller *app, bool pressed)
{
	app_lock(app);
	app->button_state = pressed;
	if (pressed) {
		app->press_count++;
		app->config_dirty = true;   /* persist the new count to NVS */
	}
	app_unlock(app);
}

bool app_get_button(struct app_controller *app)
{
	bool pressed = false;

	app_lock(app);
	pressed = app->button_state;
	app_unlock(app);
	return pressed;
}

uint32_t app_get_press_count(struct app_controller *app)
{
	uint32_t count = 0;

	app_lock(app);
	count = app->press_count;
	app_unlock(app);
	return count;
}

void app_set_press_count(struct app_controller *app, uint32_t count)
{
	app_lock(app);
	app->press_count = count;
	app_unlock(app);
}

void app_load_config(struct app_controller *app, uint16_t led_default)
{
	app_lock(app);
	app->led_default = led_default ? 1 : 0;
	app_unlock(app);
}

void app_read_config(struct app_controller *app, uint16_t *led_default)
{
	app_lock(app);
	*led_default = app->led_default;
	app_unlock(app);
}

void app_set_led_default(struct app_controller *app, uint16_t led_default)
{
	app_lock(app);
	app->led_default = led_default ? 1 : 0;
	app->config_dirty = true;
	app_unlock(app);
}

uint16_t app_get_led_default(struct app_controller *app)
{
	uint16_t led_default = 0;

	app_lock(app);
	led_default = app->led_default;
	app_unlock(app);
	return led_default;
}

bool app_consume_config_dirty(struct app_controller *app)
{
	bool dirty = false;

	app_lock(app);
	dirty = app->config_dirty;
	app->config_dirty = false;
	app_unlock(app);
	return dirty;
}

void app_set_rtc_epoch(struct app_controller *app, uint32_t epoch)
{
	app_lock(app);
	app->rtc_epoch = epoch;
	app_unlock(app);
}

uint32_t app_get_rtc_epoch(struct app_controller *app)
{
	uint32_t epoch = 0;

	app_lock(app);
	epoch = app->rtc_epoch;
	app_unlock(app);
	return epoch;
}

void app_set_time_hi(struct app_controller *app, uint16_t hi)
{
	app_lock(app);
	app->set_epoch_hi = hi;
	app_unlock(app);
}

void app_request_time_set(struct app_controller *app, uint16_t lo)
{
	app_lock(app);
	app->set_epoch = ((uint32_t)app->set_epoch_hi << 16) | lo;
	app->time_set_req = true;
	app_unlock(app);
}

uint32_t app_get_set_epoch(struct app_controller *app)
{
	uint32_t epoch = 0;

	app_lock(app);
	epoch = app->set_epoch;
	app_unlock(app);
	return epoch;
}

bool app_consume_time_set(struct app_controller *app, uint32_t *epoch)
{
	bool req = false;

	app_lock(app);
	req = app->time_set_req;
	app->time_set_req = false;
	*epoch = app->set_epoch;
	app_unlock(app);
	return req;
}
