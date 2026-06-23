#ifndef SENSOR_H_
#define SENSOR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct sensor_measures {
	int16_t temp_c100;       /* temperature, degC x 100 */
	uint16_t rh_x100;        /* relative humidity, %RH x 100 */
	uint16_t sensor_status;  /* SHT3x: 0 ok, 1 error/absent */
	uint16_t dist_mm;        /* VL53L0X distance, mm */
	uint16_t dist_status;    /* VL53L0X: 0 ok, 1 error/absent */
};

int sensor_init(void);
void sensor_update(void);
int sensor_get(struct sensor_measures *measures);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_H_ */
