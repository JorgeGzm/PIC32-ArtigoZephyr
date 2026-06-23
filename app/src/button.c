#include "button.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "app.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_button, LOG_LEVEL_INF);

#define DEBOUNCE_TICKS   2

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static int raw_prev;
static int stable;
static bool pressed;

int button_init(void)
{
	if (!gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}
	gpio_pin_configure_dt(&button, GPIO_INPUT);
	return 0;
}

void button_poll(struct app_controller *app)
{
	int raw = gpio_pin_get_dt(&button);

	if (raw == raw_prev) {
		if (stable < DEBOUNCE_TICKS) {
			stable++;
		}
	} else {
		stable = 0;
		raw_prev = raw;
	}

	if (stable == DEBOUNCE_TICKS && (raw != 0) != pressed) {
		pressed = (raw != 0);
		app_set_button(app, pressed);
	}
}
