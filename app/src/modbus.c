/**
 *
 * Register map (all addresses 0-based):
 *
 *   Coils (FC01 read / FC05 write) - RW bits
 *     0  LED control            1=on, 0=off (manual)
 *     1  Reboot command         write 1 -> system reboot
 *
 *   Discrete Inputs (FC02) - RO bits
 *     0  Button SW0 state       1=pressed
 *
 *   Input Registers (FC04) - RO 16-bit
 *     0  Uptime seconds, high word
 *     1  Uptime seconds, low word
 *     2  Button press count
 *     3  Firmware version       (0x0100 = v1.0)
 *     4  Free heap (bytes)
 *     5  SHT3x temperature      signed, degC x 100
 *     6  SHT3x humidity          %RH x 100
 *     7  VL53L0X distance       millimeters (clamped to 65535)
 *     8  RTC time, high word    epoch seconds >> 16
 *     9  RTC time, low word     epoch seconds & 0xFFFF
 *
 *   Holding Registers (FC03 read / FC06 write) - RW 16-bit (config)
 *     0  LED default state      restored on boot (0/1)   [NVS]
 *     1  Set time, high word    epoch seconds >> 16      (buffered)
 *     2  Set time, low word     epoch seconds & 0xFFFF   (write applies to RTC)
 */

#include "modbus.h"

#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_stats.h>

#include "app.h"
#include "sensor.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_modbus, LOG_LEVEL_INF);

#define MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)

extern struct k_heap _system_heap;

static uint32_t modbus_free_heap_bytes(void);
static int modbus_coil_rd(uint16_t addr, bool *state);
static int modbus_coil_wr(uint16_t addr, bool state);
static int modbus_discrete_input_rd(uint16_t addr, bool *state);
static int modbus_input_reg_rd(uint16_t addr, uint16_t *reg);
static int modbus_holding_reg_rd(uint16_t addr, uint16_t *reg);
static int modbus_holding_reg_wr(uint16_t addr, uint16_t reg);

static struct modbus_user_callbacks mbs_cbs = {
	.coil_rd = modbus_coil_rd,
	.coil_wr = modbus_coil_wr,
	.discrete_input_rd = modbus_discrete_input_rd,
	.input_reg_rd = modbus_input_reg_rd,
	.holding_reg_rd = modbus_holding_reg_rd,
	.holding_reg_wr = modbus_holding_reg_wr,
};

/* Controller handle, stashed at start; the fixed-signature callbacks below
 * cannot take it as a parameter, so they read it from here. */
static struct app_controller *app_ctrl;

static uint32_t modbus_free_heap_bytes(void)
{
	struct sys_memory_stats stats = {0};

	if (sys_heap_runtime_stats_get(&_system_heap.heap, &stats) != 0) {
		return 0;
	}
	return (uint32_t)stats.free_bytes;
}

static int modbus_coil_rd(uint16_t addr, bool *state)
{
	switch (addr) {
	case 0: *state = app_get_led(app_ctrl); break;
	case 1: *state = app_reboot_requested(app_ctrl); break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static int modbus_coil_wr(uint16_t addr, bool state)
{
	switch (addr) {
	case 0:
		app_set_led(app_ctrl, state);
		break;
	case 1:
		if (state) {
			app_request_reboot(app_ctrl);
		}
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static int modbus_discrete_input_rd(uint16_t addr, bool *state)
{
	if (addr != 0) {
		return -ENOTSUP;
	}
	*state = app_get_button(app_ctrl);
	return 0;
}

static int modbus_input_reg_rd(uint16_t addr, uint16_t *reg)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000);
	struct sensor_measures meas = {0};

	sensor_get(&meas);

	switch (addr) {
	case 0: *reg = (uint16_t)(uptime_s >> 16); break;
	case 1: *reg = (uint16_t)(uptime_s & 0xFFFF); break;
	case 2: *reg = (uint16_t)app_get_press_count(app_ctrl); break;
	case 3: *reg = APP_FW_VERSION; break;
	case 4: *reg = (uint16_t)MIN(modbus_free_heap_bytes(), 0xFFFF); break;
	case 5: *reg = (uint16_t)meas.temp_c100; break;
	case 6: *reg = meas.rh_x100; break;
	case 7: *reg = meas.dist_mm; break;
	case 8: *reg = (uint16_t)(app_get_rtc_epoch(app_ctrl) >> 16); break;
	case 9: *reg = (uint16_t)(app_get_rtc_epoch(app_ctrl) & 0xFFFF); break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static int modbus_holding_reg_rd(uint16_t addr, uint16_t *reg)
{
	switch (addr) {
	case 0: *reg = app_get_led_default(app_ctrl); break;
	case 1: *reg = (uint16_t)(app_get_set_epoch(app_ctrl) >> 16); break;
	case 2: *reg = (uint16_t)(app_get_set_epoch(app_ctrl) & 0xFFFF); break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static int modbus_holding_reg_wr(uint16_t addr, uint16_t reg)
{
	switch (addr) {
	case 0:
		app_set_led_default(app_ctrl, reg);
		break;
	case 1:
		/* buffer the high word; the low-word write applies the time */
		app_set_time_hi(app_ctrl, reg);
		break;
	case 2:
		app_request_time_set(app_ctrl, reg);
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

int modbus_server_start(struct app_controller *app)
{
	const char iface_name[] = {DEVICE_DT_NAME(MODBUS_NODE)};
	int iface = modbus_iface_get_by_name(iface_name);
	struct modbus_iface_param param = {
		.mode = MODBUS_MODE_RTU,
		.server = {
			.user_cb = &mbs_cbs,
			.unit_id = MODBUS_UNIT_ID,
		},
		.serial = {
			.baud = 115200,
			.parity = UART_CFG_PARITY_NONE,
			.stop_bits = UART_CFG_STOP_BITS_1,
		},
	};

	if (iface < 0) {
		LOG_ERR("No Modbus iface for %s (%d)", iface_name, iface);
		return iface;
	}

	app_ctrl = app;
	return modbus_init_server(iface, param);
}
