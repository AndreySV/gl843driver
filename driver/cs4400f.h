#ifndef _CS4400F_H_
#define _CS4400F_H_

#include "defs.h"

float __attribute__ ((pure)) max_afe_gain();
float __attribute__ ((pure)) min_afe_gain();
int __attribute__ ((pure)) afe_gain_to_val(float g);
int write_afe_gain(struct gl843_device *dev, int i, float g);

int set_lamp(struct gl843_device *dev, enum gl843_lamp state, int timeout);

int setup_base(struct gl843_device *dev);
int setup_frontend(struct gl843_device *dev, struct scan_setup *ss);
int setup_motor(struct gl843_device *dev, struct scan_setup *ss);
int move_scanner_head(struct gl843_device *dev, float d);
int select_shading(struct gl843_device *dev, enum gl843_shading mode);

#endif /* _CS4400F_H_ */
