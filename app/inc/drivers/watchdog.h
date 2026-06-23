#ifndef DRIVERS_WATCHDOG_H_
#define DRIVERS_WATCHDOG_H_

#ifdef __cplusplus
extern "C" {
#endif

int watchdog_init(void);
void watchdog_kick(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_WATCHDOG_H_ */
