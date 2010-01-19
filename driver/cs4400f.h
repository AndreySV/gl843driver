#ifndef _CS4400F_H_
#define _CS4400F_H_

#include "defs.h"

float __attribute__ ((pure)) max_afe_gain();
float __attribute__ ((pure)) min_afe_gain();
int __attribute__ ((pure)) afe_gain_to_val(float g);
int write_afe_gain(struct gl843_device *dev, int i, float g);

int setup_static(struct gl843_device *dev);
int setup_common(struct gl843_device *dev, struct scan_setup *ss);
int setup_vertical(struct gl843_device *dev, struct scan_setup *ss, int calibrate);
int setup_horizontal(struct gl843_device *dev, struct scan_setup *ss);

int select_shading(struct gl843_device *dev, enum gl843_shading mode);
int set_lamp(struct gl843_device *dev, enum gl843_lamp state, int timeout);

int move_scanner_head(struct gl843_device *dev, float d);

#endif /* _CS4400F_H_ */
