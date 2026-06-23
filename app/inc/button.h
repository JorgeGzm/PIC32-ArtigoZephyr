#ifndef BUTTON_H_
#define BUTTON_H_

#ifdef __cplusplus
extern "C" {
#endif

struct app_controller;

int button_init(void);
void button_poll(struct app_controller *app);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H_ */
