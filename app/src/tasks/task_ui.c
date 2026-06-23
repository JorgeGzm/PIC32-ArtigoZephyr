#include "display.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "app.h"
#include "sensor.h"
#include "tasks/tasks.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_display, LOG_LEVEL_INF);

/* MCP23008 pin map (matches the STM32 HW-LYT IHM):
 *   P0..P3 = 4 status LEDs, P4..P7 = keys. */
#define LED_PIN_0       0
#define LED_PIN_1       1
#define LED_PIN_2       2
#define LED_PIN_3       3
#define KEY_UP_PIN      4
#define KEY_DOWN_PIN    5
#define KEY_CANCEL_PIN  6
#define KEY_ENTER_PIN   7

#define SHIELD_LED_ON   1
#define SHIELD_LED_OFF  0

#define OLED_W   128

#define DISPLAY_STACK_SIZE   1024
#define DISPLAY_PRIORITY     6

/* Navigation menu: one screen at a time, changed with the LEFT/RIGHT keys.
 * The HW-LYT shield only has UP/DOWN/CANCEL/ENTER, so UP acts as LEFT
 * (previous screen) and DOWN as RIGHT (next screen). */
enum { SCR_DIST = 0, SCR_ENV, SCR_MODBUS, SCR_CLOCK, SCR_COUNT };

static void key_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void ui_pick_fonts(void);
static void ui_center(uint8_t font, uint8_t char_w, int16_t y, const char *s);
static void shield_leds_all(bool on);
static void shield_led_only(uint8_t idx);
static char ui_scan_keys(void);
static void ui_handle_keys(void);

static const struct device *const oled = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const keypad = DEVICE_DT_GET(DT_ALIAS(keypad));

K_THREAD_STACK_DEFINE(display_stack, DISPLAY_STACK_SIZE);
static struct k_thread display_thread;

/* Redraw request: given by main() (new sensor data / 1 Hz tick) and by the
 * keypad ISR (key edge); taken by the display task. Binary (coalescing). */
static K_SEM_DEFINE(display_sem, 0, 1);

static const char *const ui_title[SCR_COUNT] = {
	"Distance",
	"Temp/Humid.",
	"Modbus",
	"Clock",
};

/* Which shield LED (MCP P0..P3) lights for each screen. */
static const uint8_t shield_led_pin[SCR_COUNT] = {
	LED_PIN_0,   /* SCR_DIST   */
	LED_PIN_1,   /* SCR_ENV    */
	LED_PIN_2,   /* SCR_MODBUS */
	LED_PIN_3,   /* SCR_CLOCK  */
};

static struct app_controller *app_ctrl;   /* stashed at ui_init() */
static bool ui_ready;
static uint8_t ui_screen;   /* current screen index */
static char ui_last_key;    /* for press-edge detection */

/* Single font (the smallest available) for header + body; picked at runtime
 * so we don't hard-code a CFB font index. */
static uint8_t font_small;
static uint8_t fw_small, fh_small;

/* Key events come from the MCP23008 INT line (event-driven, no polling):
 * the driver fires this callback in its interrupt-handler thread context
 * (the INT is read back over I2C), so it is safe to publish to zbus here.
 * The display task scans the keypad when it receives the event. */
static struct gpio_callback key_cb;

static void key_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	k_sem_give(&display_sem);
}

static void ui_pick_fonts(void)
{
	uint8_t n = cfb_get_numof_fonts(oled);
	uint8_t w = 0;
	uint8_t h = 0;
	uint8_t smallest = 255;

	for (uint8_t i = 0; i < n; i++) {
		if (cfb_get_font_size(oled, i, &w, &h) != 0) {
			continue;
		}
		if (h < smallest) {
			smallest = h;
			font_small = i;
			fw_small = w;
			fh_small = h;
		}
	}
}

/* Print a string horizontally centered at row y, in the given font. */
static void ui_center(uint8_t font, uint8_t char_w, int16_t y, const char *s)
{
	int16_t x = (OLED_W - (int16_t)strlen(s) * char_w) / 2;

	if (x < 0) {
		x = 0;
	}
	cfb_framebuffer_set_font(oled, font);
	cfb_print(oled, s, x, y);
}

static void shield_leds_all(bool on)
{
	if (!device_is_ready(keypad)) {
		return;
	}
	for (uint8_t p = LED_PIN_0; p <= LED_PIN_3; p++) {
		gpio_pin_set(keypad, p, on ? SHIELD_LED_ON : SHIELD_LED_OFF);
	}
}

static void shield_led_only(uint8_t idx)
{
	if (!device_is_ready(keypad)) {
		return;
	}
	for (uint8_t p = LED_PIN_0; p <= LED_PIN_3; p++) {
		gpio_pin_set(keypad, p, (p == idx) ? SHIELD_LED_ON : SHIELD_LED_OFF);
	}
}

/* Returns a key char ('U','D','C','E') for the first pressed key, or 0.
 * Pressed = logical 0 (active-low with pull-up). */
static char ui_scan_keys(void)
{
	if (!device_is_ready(keypad)) {
		return 0;
	}
	if (gpio_pin_get(keypad, KEY_ENTER_PIN) == 0) {
		return 'E';
	}
	if (gpio_pin_get(keypad, KEY_CANCEL_PIN) == 0) {
		return 'C';
	}
	if (gpio_pin_get(keypad, KEY_UP_PIN) == 0) {
		return 'U';
	}
	if (gpio_pin_get(keypad, KEY_DOWN_PIN) == 0) {
		return 'D';
	}
	return 0;
}

/* Handle a key edge: LEFT (UP) = previous screen, RIGHT (DOWN) = next. */
static void ui_handle_keys(void)
{
	char k = ui_scan_keys();

	if (k != 0 && k != ui_last_key) {
		if (k == 'U') {            /* LEFT */
			ui_screen = (ui_screen + SCR_COUNT - 1) % SCR_COUNT;
			ui_render();
			ui_update_leds();
		} else if (k == 'D') {     /* RIGHT */
			ui_screen = (ui_screen + 1) % SCR_COUNT;
			ui_render();
			ui_update_leds();
		}
	}
	ui_last_key = k;
}

int ui_init(struct app_controller *app)
{
	const uint32_t key_mask = BIT(KEY_UP_PIN) | BIT(KEY_DOWN_PIN) |
				  BIT(KEY_CANCEL_PIN) | BIT(KEY_ENTER_PIN);

	app_ctrl = app;

	if (!device_is_ready(oled)) {
		LOG_ERR("OLED not ready");
		return -ENODEV;
	}
	if (cfb_framebuffer_init(oled) != 0) {
		LOG_ERR("CFB init failed");
		return -EIO;
	}
	
	display_blanking_off(oled);
	ui_pick_fonts();
	cfb_framebuffer_clear(oled, true);
	cfb_framebuffer_finalize(oled);
	ui_ready = true;

	if (device_is_ready(keypad)) {
		/* 4 keys as inputs w/ pull-up (buttons pull the line low when pressed). */
		gpio_pin_configure(keypad, KEY_UP_PIN, GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_configure(keypad, KEY_DOWN_PIN, GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_configure(keypad, KEY_CANCEL_PIN, GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_configure(keypad, KEY_ENTER_PIN, GPIO_INPUT | GPIO_PULL_UP);
		/* 4 status LEDs as outputs (off). */
		gpio_pin_configure(keypad, LED_PIN_0, GPIO_OUTPUT_INACTIVE);
		gpio_pin_configure(keypad, LED_PIN_1, GPIO_OUTPUT_INACTIVE);
		gpio_pin_configure(keypad, LED_PIN_2, GPIO_OUTPUT_INACTIVE);
		gpio_pin_configure(keypad, LED_PIN_3, GPIO_OUTPUT_INACTIVE);

		/* Event-driven keys: interrupt-on-change via the MCP INT line.
		 * mcp23xxx only does edge-both in hardware, so we use EDGE_BOTH
		 * and let ui_handle_keys() do press-edge detection in software. */
		gpio_init_callback(&key_cb, key_isr, key_mask);
		gpio_add_callback(keypad, &key_cb);
		gpio_pin_interrupt_configure(keypad, KEY_UP_PIN, GPIO_INT_EDGE_BOTH);
		gpio_pin_interrupt_configure(keypad, KEY_DOWN_PIN, GPIO_INT_EDGE_BOTH);
		gpio_pin_interrupt_configure(keypad, KEY_CANCEL_PIN, GPIO_INT_EDGE_BOTH);
		gpio_pin_interrupt_configure(keypad, KEY_ENTER_PIN, GPIO_INT_EDGE_BOTH);
	} else {
		LOG_ERR("keypad (MCP23008) not ready");
	}

	return 0;
}

void ui_update_leds(void)
{
	if (!device_is_ready(keypad)) {
		return;
	}
	for (uint8_t s = 0; s < SCR_COUNT; s++) {
		gpio_pin_set(keypad, shield_led_pin[s],
			     (s == ui_screen) ? SHIELD_LED_ON : SHIELD_LED_OFF);
	}
}

void ui_boot_splash(void)
{
	static const char msg[] = "PIC32MC";
	char buf[sizeof(msg)];
	size_t n = strlen(msg);

	if (!ui_ready) {
		return;
	}

	/* Type the message letter by letter, LEDs chasing P0->P3. */
	for (size_t i = 1; i <= n; i++) {
		memcpy(buf, msg, i);
		buf[i] = '\0';

		cfb_framebuffer_clear(oled, false);
		ui_center(font_small, fw_small, 0, "BOOT");
		ui_center(font_small, fw_small, 28, buf);
		cfb_invert_area(oled, 0, 0, OLED_W, fh_small);
		cfb_framebuffer_finalize(oled);

		shield_led_only(LED_PIN_0 + (uint8_t)((i - 1) % 4));
		k_msleep(160);
	}

	/* Final flourish: blink all 4 LEDs together a few times. */
	for (int b = 0; b < 3; b++) {
		shield_leds_all(true);
		k_msleep(120);
		shield_leds_all(false);
		k_msleep(120);
	}
}

void ui_render(void)
{
	char l1[18] = {0};
	char l2[18] = {0};
	int16_t t100 = 0;
	uint16_t rh100 = 0;
	uint16_t dist = 0;
	uint32_t epoch = 0;
	time_t epoch_time = 0;
	struct sensor_measures meas = {0};
	struct tm tm_buf = {0};

	if (!ui_ready) {
		return;
	}

	sensor_get(&meas);
	t100 = meas.temp_c100;
	rh100 = meas.rh_x100;
	dist = meas.dist_mm;

	epoch = app_get_rtc_epoch(app_ctrl);

	switch (ui_screen) {
	case SCR_DIST:
		snprintf(l1, sizeof(l1), "%u mm", dist);
		break;
	case SCR_ENV:
		snprintf(l1, sizeof(l1), "T:%d.%01d C",
			 t100 / 100, (t100 < 0 ? -t100 : t100) % 100 / 10);
		snprintf(l2, sizeof(l2), "H:%u.%01u %%",
			 rh100 / 100U, rh100 % 100U / 10U);
		break;
	case SCR_MODBUS:
		snprintf(l1, sizeof(l1), "Addr: %u", MODBUS_UNIT_ID);
		snprintf(l2, sizeof(l2), "115200 8N1");
		break;
	case SCR_CLOCK:
		epoch_time = (time_t)epoch;
		gmtime_r(&epoch_time, &tm_buf);
		snprintf(l1, sizeof(l1), "%02d:%02d:%02d",
			 tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
		snprintf(l2, sizeof(l2), "%02u/%02u/%04u",
			 (unsigned)tm_buf.tm_mday % 100U,
			 ((unsigned)tm_buf.tm_mon + 1U) % 100U,
			 ((unsigned)tm_buf.tm_year + 1900U) % 10000U);
		break;
	default:
		break;
	}

	cfb_framebuffer_clear(oled, false);

	/* Title centered on the header band, then invert the band -> the
	 * STM32-IHM look: white bar with black title text. */
	ui_center(font_small, fw_small, 0, ui_title[ui_screen]);

	/* Body values centered, in the small font. */
	if (l1[0] != '\0') {
		ui_center(font_small, fw_small, 24, l1);
	}
	if (l2[0] != '\0') {
		ui_center(font_small, fw_small, 48, l2);
	}

	cfb_invert_area(oled, 0, 0, OLED_W, fh_small);
	cfb_framebuffer_finalize(oled);
}

void ui_service_keys(void)
{
	ui_handle_keys();
}

void ui_notify(void)
{
	k_sem_give(&display_sem);
}

static void display_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ui_boot_splash();
	ui_render();
	ui_update_leds();

	while (1) {
		if (k_sem_take(&display_sem, K_FOREVER) != 0) {
			continue;
		}

		/* Woken by a key edge and/or new sensor data / 1 Hz tick:
		 * handle any key navigation, then redraw the current screen. */
		ui_service_keys();
		ui_render();
	}
}

void task_display_start(struct app_controller *app)
{
	ARG_UNUSED(app);

	k_thread_create(&display_thread, display_stack,
			K_THREAD_STACK_SIZEOF(display_stack),
			display_entry, NULL, NULL, NULL,
			DISPLAY_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&display_thread, "display");
}
