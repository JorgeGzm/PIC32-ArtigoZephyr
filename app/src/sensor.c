#include "sensor.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_sensor, LOG_LEVEL_INF);

struct sensor_state {
	struct k_mutex lock;
	struct sensor_measures measures;
};

static const struct device *const sht3x = DEVICE_DT_GET(DT_ALIAS(sht3x));
static const struct device *const vl53 = DEVICE_DT_GET(DT_ALIAS(tof0));

static struct sensor_state sensor;

int sensor_init(void)
{
	int err = -EIO;
	
	k_mutex_init(&sensor.lock);

	if (!device_is_ready(sht3x)) {
		LOG_WRN("SHT3x not ready");
	}
	else if (!device_is_ready(vl53)) {
		LOG_WRN("VL53L0X not ready");
	}
	else {
		return err = 0;
	}

	return err;
}

void sensor_update(void)
{
	int16_t t100 = 0;
	uint16_t rh100 = 0;
	uint16_t dist_mm = 0;
	uint16_t sht_status = 1;
	uint16_t tof_status = 1;
	uint32_t raw_mm = 0;
	struct sensor_value temp = {0};
	struct sensor_value humidity = {0};
	struct sensor_value distance = {0};

	/* SHT3x temperature / humidity. */
	if (sensor_sample_fetch(sht3x) == 0 &&
	    sensor_channel_get(sht3x, SENSOR_CHAN_AMBIENT_TEMP, &temp) == 0 &&
	    sensor_channel_get(sht3x, SENSOR_CHAN_HUMIDITY, &humidity) == 0) {
		t100 = (int16_t)(sensor_value_to_double(&temp) * 100.0);
		rh100 = (uint16_t)(sensor_value_to_double(&humidity) * 100.0);
		sht_status = 0;
	}

	/* VL53L0X distance (DISTANCE channel: val1=m, val2=micro-m). */
	if (sensor_sample_fetch(vl53) == 0 &&
	    sensor_channel_get(vl53, SENSOR_CHAN_DISTANCE, &distance) == 0) {
		raw_mm = (uint32_t)distance.val1 * 1000U +
			 (uint32_t)distance.val2 / 1000U;
		dist_mm = (uint16_t)MIN(raw_mm, 0xFFFFU);
		tof_status = 0;
	}

	k_mutex_lock(&sensor.lock, K_FOREVER);
	sensor.measures.temp_c100 = t100;
	sensor.measures.rh_x100 = rh100;
	sensor.measures.sensor_status = sht_status;
	sensor.measures.dist_mm = dist_mm;
	sensor.measures.dist_status = tof_status;
	k_mutex_unlock(&sensor.lock);

	/* Per-read values at DEBUG level (off by default; raise the module log
	 * level to see them). Integer math: no float in LOG_MODE_MINIMAL. */
	LOG_DBG("SHT3x: temp=%d.%02d C  rh=%u.%02u %%  | VL53L0X: dist=%u mm (st=%u)",
		t100 / 100, (t100 < 0 ? -t100 : t100) % 100,
		rh100 / 100U, rh100 % 100U, dist_mm, tof_status);
}

int sensor_get(struct sensor_measures *measures)
{
	if (measures == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&sensor.lock, K_FOREVER);
	*measures = sensor.measures;
	k_mutex_unlock(&sensor.lock);
	return 0;
}
