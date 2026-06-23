#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include "app.h"
#include "button.h"
#include "display.h"
#include "drivers/rtc.h"
#include "drivers/watchdog.h"
#include "sensor.h"
#include "setup/setup_hw.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define LOOP_TICK_MS      20      /* fast tick: watchdog + responsive button */
#define SENSOR_PERIOD_MS  1000    /* sensor read + RTC refresh + display redraw */

/* Apply the deferred Modbus requests outside the callback context. */
static void service_modbus_cmds(struct app_controller *app)
{
	uint32_t epoch = 0;

	if (app_consume_time_set(app, &epoch)) {
		rtc_write_epoch(epoch);
	}
	if (app_consume_config_dirty(app)) {
		setup_config_save(app);
	}
	if (app_reboot_requested(app)) {
		LOG_WRN("Reboot requested over Modbus");
		k_msleep(50);
		sys_reboot(SYS_REBOOT_COLD);
	}
}

int main(void)
{
	struct app_controller *app = NULL;
	int64_t next_sensor_ms = 0;

	if (setup_init() != 0) {
		return 0;
	}

	app = setup_app();

	while (1) {
		watchdog_kick();

		/* Board button: sampled + debounced every fast tick (20 ms) so it
		 * stays responsive. */
		button_poll(app);

		/* Sensors + RTC + display refresh, gated to ~1 s (the I2C reads
		 * are slow, no need to do them every tick). */
		if (k_uptime_get() >= next_sensor_ms) {
			uint32_t epoch = 0;

			sensor_update();
			if (rtc_read_epoch(&epoch) == 0) {
				app_set_rtc_epoch(app, epoch);
			}
			ui_notify();

			next_sensor_ms = k_uptime_get() + SENSOR_PERIOD_MS;
		}

		/* Deferred Modbus commands (time-set / config save / reboot). */
		service_modbus_cmds(app);

		k_msleep(LOOP_TICK_MS);
	}

	return 0;
}
