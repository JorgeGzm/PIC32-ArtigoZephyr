#include "drivers/rtc.h"

#include <time.h>

#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(drv_rtc, LOG_LEVEL_INF);

static const struct device *const rtc = DEVICE_DT_GET(DT_NODELABEL(rtc));

int rtc_init(void)
{
	if (!device_is_ready(rtc)) {
		return -EIO;
	}
	return 0;
}

int rtc_read_epoch(uint32_t *epoch)
{
	struct rtc_time rt;

	if (!device_is_ready(rtc) || rtc_get_time(rtc, &rt) != 0) {
		return -EIO;
	}
	*epoch = (uint32_t)timeutil_timegm(rtc_time_to_tm(&rt));
	return 0;
}

void rtc_write_epoch(uint32_t epoch)
{
	time_t t = (time_t)epoch;
	struct tm tm_buf = {0};
	struct rtc_time rt = {0};

	if (!device_is_ready(rtc)) {
		return;
	}
	gmtime_r(&t, &tm_buf);
	rt.tm_sec  = tm_buf.tm_sec;
	rt.tm_min  = tm_buf.tm_min;
	rt.tm_hour = tm_buf.tm_hour;
	rt.tm_mday = tm_buf.tm_mday;
	rt.tm_mon  = tm_buf.tm_mon;
	rt.tm_year = tm_buf.tm_year;
	rt.tm_wday = tm_buf.tm_wday;
	rt.tm_yday = tm_buf.tm_yday;

	if (rtc_set_time(rtc, &rt) != 0) {
		LOG_ERR("rtc_set_time failed");
	} else {
		LOG_INF("RTC set to epoch %u", epoch);
	}
}
