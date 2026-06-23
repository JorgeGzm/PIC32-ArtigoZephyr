#ifndef MODBUS_APP_H_
#define MODBUS_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

struct app_controller;

int modbus_server_start(struct app_controller *app);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_APP_H_ */
