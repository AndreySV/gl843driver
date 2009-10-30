#ifndef _CS4400F_H_
#define _CS4400F_H_

#include "image.h"
#include "motor.h"

enum gl843_lamp
{
	LAMP_OFF,
	LAMP_PLATEN,	/* Flatbed lamp */
	LAMP_TA		/* Transparency adapter lamp */
};

enum gl843_sysclk {
	SYSCLK_24_MHZ = 0,
	SYSCLK_30_MHZ = 1,
	SYSCLK_40_MHZ = 2,
	SYSCLK_48_MHZ = 3,
	SYSCLK_60_MHZ = 4
};

int do_base_configuration(struct gl843_device *dev);
int set_lamp(struct gl843_device *dev, enum gl843_lamp state, int timeout);
int setup_ccd_and_afe(struct gl843_device *dev,
			enum gl843_pixformat fmt,
			unsigned int start_x,
			unsigned int width,
			int dpi,
			int afe_dpi,
			int linesel,
			int tgtime, int lperiod,
			int expr, int expg, int expb);
int set_postprocessing(struct gl843_device *dev);
void set_motor(struct gl843_device *dev);
void setup_scanner(struct gl843_device *dev);

void cs4400f_build_motor_table(struct gl843_motor_setting *m,
	unsigned int speed, enum motor_step step);
void get_fastest_motor_table(struct gl843_motor_setting *m);

#endif /* _CS4400F_H_ */
