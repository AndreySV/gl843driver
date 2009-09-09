#ifndef _GL843_CS4400F_H_
#define _GL843_CS4400F_H_

#include "gl843_motor.h"

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

void set_sysclk(struct gl843_device *dev, enum gl843_sysclk clkset);
void set_lamp(struct gl843_device *dev, enum gl843_lamp state, int timeout);
void set_frontend(struct gl843_device *dev,
		  int afe_dpi, int deep_color,
		  int linesel, int maxwd,
		  int tgtime, int lperiod,
		  int expr, int expg, int expb);
void set_motor(struct gl843_device *dev);
void setup_scanner(struct gl843_device *dev);

void cs4400f_build_motor_table(struct gl843_motor_setting *m,
	unsigned int speed, enum motor_step step);




#endif /* _GL843_CS4400F_H_ */
