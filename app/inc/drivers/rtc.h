#ifndef DRIVERS_RTC_H_
#define DRIVERS_RTC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int rtc_init(void);
int rtc_read_epoch(uint32_t *epoch);
void rtc_write_epoch(uint32_t epoch);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_RTC_H_ */
