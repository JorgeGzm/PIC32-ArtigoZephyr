#include "setup/setup_hw.h"
#include <errno.h>

#include <soc.h>                 /* SAMC21 RSTC->RCAUSE (reset cause) */

#include <zephyr/app_version.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/version.h>
#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>

#include "sensor.h"
#include "app.h"
#include "button.h"
#include "display.h"
#include "drivers/rtc.h"
#include "drivers/watchdog.h"
#include "modbus.h"
#include "tasks/tasks.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(setup, LOG_LEVEL_INF);

#define NVS_PARTITION   storage_partition
#define CONFIG_NVS_ID   1U

struct app_config {
	uint16_t led_default;
	uint32_t press_count;
};

static struct app_controller *app_ctrl;

static struct nvs_fs cfg_fs;
static bool cfg_nvs_ready;

static int config_nvs_init(void);
static void config_load(struct app_controller *app);

static void setup_sanity(void);
static int setup_init_middleware(void);
static int setup_init_database(void);
static int setup_init_tasks(void);
static int setup_fatal_error(int error);

static int config_nvs_init(void)
{
	int rc = 0;
	struct flash_pages_info info;

	cfg_fs.flash_device = PARTITION_DEVICE(NVS_PARTITION);
	if (!device_is_ready(cfg_fs.flash_device)) {
		LOG_ERR("NVS flash device not ready");
		return -ENODEV;
	}

	cfg_fs.offset = PARTITION_OFFSET(NVS_PARTITION);
	rc = flash_get_page_info_by_offs(cfg_fs.flash_device, cfg_fs.offset, &info);
	if (rc) {
		LOG_ERR("NVS page info failed (%d)", rc);
		return rc;
	}

	cfg_fs.sector_size = info.size;
	cfg_fs.sector_count = DT_REG_SIZE(DT_NODELABEL(storage_partition)) / info.size;

	rc = nvs_mount(&cfg_fs);
	if (rc) {
		LOG_ERR("NVS mount failed (%d)", rc);
		return rc;
	}

	cfg_nvs_ready = true;
	return 0;
}

static void config_load(struct app_controller *app)
{
	struct app_config cfg;

	if (!cfg_nvs_ready) {
		return;
	}
	if (nvs_read(&cfg_fs, CONFIG_NVS_ID, &cfg, sizeof(cfg)) == sizeof(cfg)) {
		app_load_config(app, cfg.led_default);
		app_set_press_count(app, cfg.press_count);
		LOG_INF("Config loaded from NVS (presses=%u)", cfg.press_count);
	} else {
		LOG_INF("No saved config, using defaults");
	}
}

void setup_config_save(struct app_controller *app)
{
	struct app_config cfg = {0};

	if (!cfg_nvs_ready) {
		return;
	}
	app_read_config(app, &cfg.led_default);
	cfg.press_count = app_get_press_count(app);

	if (nvs_write(&cfg_fs, CONFIG_NVS_ID, &cfg, sizeof(cfg)) < 0) {
		LOG_ERR("NVS write failed");
	} else {
		LOG_INF("Config saved to NVS");
	}
}

int setup_config_read_saved(uint16_t *led_default, uint32_t *press_count)
{
	struct app_config cfg;

	if (!cfg_nvs_ready) {
		return -ENODEV;
	}
	if (nvs_read(&cfg_fs, CONFIG_NVS_ID, &cfg, sizeof(cfg)) != sizeof(cfg)) {
		return -ENOENT;
	}
	if (led_default != NULL) {
		*led_default = cfg.led_default;
	}
	if (press_count != NULL) {
		*press_count = cfg.press_count;
	}
	return 0;
}

static void setup_sanity(void)
{
	uint8_t rc = RSTC->RCAUSE.reg;

	/* Why the SoC last reset (SAMC21 RSTC->RCAUSE — Zephyr's SAM0 hwinfo
	 * driver only exposes the device ID, not the reset cause). Spotting a
	 * WDT reset here is invaluable in the field. */
	printk("reset cause: 0x%02x%s%s%s%s%s%s\n", rc,
		(rc & RSTC_RCAUSE_POR)     ? " POR" : "",
		(rc & RSTC_RCAUSE_BODCORE) ? " BODCORE" : "",
		(rc & RSTC_RCAUSE_BODVDD)  ? " BODVDD" : "",
		(rc & RSTC_RCAUSE_EXT)     ? " EXT" : "",
		(rc & RSTC_RCAUSE_WDT)     ? " WDT" : "",
		(rc & RSTC_RCAUSE_SYST)    ? " SYST" : "");
}

static int setup_init_middleware(void)
{
	int err = 0;

	app_ctrl = app_init();
	if (app_ctrl == NULL) {
		LOG_ERR("app controller init failed");
		return -ENODEV;
	}

	err = sensor_init();
	if (err) {
		LOG_ERR("sensor init failed");
		return err;
	}

	err = button_init();
	if (err) {
		LOG_ERR("button init failed");
		return err;
	}

	err = rtc_init();
	if (err) {
		LOG_ERR("RTC init failed");
		return err;
	}

	err = watchdog_init();
	if (err) {
		LOG_ERR("watchdog init failed");
		return err;
	}

	err = ui_init(app_ctrl);
	if (err) {
		LOG_ERR("UI init failed");
		return err;
	}

	return 0;
}

static int setup_init_database(void)
{
	config_nvs_init();
	config_load(app_ctrl);
	app_set_led(app_ctrl, app_get_led_default(app_ctrl) != 0);

	return 0;
}

static int setup_init_tasks(void)
{
	int err;

	task_display_start(app_ctrl);

	err = modbus_server_start(app_ctrl);
	if (err) {
		LOG_ERR("Modbus server failed to start");
	} else {
		LOG_INF("Modbus RTU server up (unit id %d)", MODBUS_UNIT_ID);
	}

	return 0;
}

static int setup_fatal_error(int error)
{
	LOG_ERR("%s: phase failed with %d", __func__, error);
	return error;
}

struct app_controller *setup_app(void)
{
	return app_ctrl;
}

int setup_init(void)
{
	int err = 0;

	printk("ZPHR-PIC32-0001 FW v%s (fw_reg 0x%04x) | Zephyr %s\n",
		    APP_VERSION_STRING, APP_FW_VERSION, KERNEL_VERSION_STRING);

	setup_sanity();

	err = setup_init_middleware();
	if (err) {
		return setup_fatal_error(err);
	}

	err = setup_init_database();
	if (err) {
		return setup_fatal_error(err);
	}

	err = setup_init_tasks();
	if (err) {
		return setup_fatal_error(err);
	}

	return 0;
}
