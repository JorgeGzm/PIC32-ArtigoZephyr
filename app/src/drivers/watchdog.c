#include "drivers/watchdog.h"

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(drv_wdt, LOG_LEVEL_INF);

#if IS_ENABLED(CONFIG_WATCHDOG)

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>

#define WDT_TIMEOUT_MS   8000    /* SAM0 WDT period (8 s; 5 s rounds up) */

static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));

static int wdt_channel = -1;

int watchdog_init(void)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_SOC,
		.window = { .min = 0, .max = WDT_TIMEOUT_MS },
		.callback = NULL,
	};

	if (!device_is_ready(wdt)) {
		LOG_ERR("WDT not ready");
		return -ENODEV;
	}

	wdt_channel = wdt_install_timeout(wdt, &cfg);
	if (wdt_channel < 0) {
		LOG_ERR("wdt_install_timeout failed (%d)", wdt_channel);
		return wdt_channel;
	}

	if (wdt_setup(wdt, 0) != 0) {
		LOG_ERR("wdt_setup failed");
		wdt_channel = -1;
		return -EIO;
	}
	return 0;
}

void watchdog_kick(void)
{
	if (wdt_channel >= 0) {
		wdt_feed(wdt, wdt_channel);
	}
}

#else /* !CONFIG_WATCHDOG */

int watchdog_init(void)
{
	LOG_INF("watchdog disabled (CONFIG_WATCHDOG=n)");
	return 0;
}

void watchdog_kick(void)
{
}

#endif /* CONFIG_WATCHDOG */
