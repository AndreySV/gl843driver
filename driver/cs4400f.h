#ifndef _CS4400F_H_
#define _CS4400F_H_

#include "defs.h"

typedef struct CanoScan4400F_Scanner {

	struct gl843_device *dev;

	enum gl843_lamp lamp;
	unsigned int lamp_timeout;

	enum gl843_pixformat fmt;
	unsigned int x_dpi;
	unsigned int y_dpi;
	unsigned int start_x;
	unsigned int start_y;
	unsigned int width;
	unsigned int height;

	uint8_t afe_offset[3];	/* AFE offset register */
	float afe_gain[3];	/* AFE gain */
	uint16_t *shcorr;	/* Shading correction */
	size_t shcorr_len;	/* Number of bytes in shading correction */

	/* State flags */
#define CS4400F_LAMP_WARM
#define CS4400F_CALIBRATED
#define CS4400F_

} CanoScan4400F_Scanner;

float __attribute__ ((pure)) max_afe_gain();
float __attribute__ ((pure)) min_afe_gain();
int __attribute__ ((pure)) afe_gain_to_val(float g);
int write_afe_gain(struct gl843_device *dev, int i, float g);

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
int select_shading(struct gl843_device *dev, enum gl843_shading mode);
void set_motor(struct gl843_device *dev);
void setup_scanner(struct gl843_device *dev);

void build_accel_profile(struct motor_accel *m,
	uint16_t c_start, uint16_t c_end, float exp);


#endif /* _CS4400F_H_ */
