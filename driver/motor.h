#ifndef _GL843_MOTOR_H_
#define _GL843_MOTOR_H_

enum motor_step
{
	FULL_STEP = 0,
	HALF_STEP = 1,
	QUARTER_STEP = 2,
	EIGHTH_STEP = 3
};

/* This must be 2. See build_motor_table() */
#define STEPTIM 2
/* Must be 1020 (hardware limit). */
#define MTRTBL_SIZE 1020

struct gl843_motor_setting
{
	enum motor_step type;	/* Step size. */
	uint16_t c_max;		/* Start speed [counter ticks per step]. */
	uint16_t c_min;		/* End speed [counter ticks per step]. */
	unsigned int alen;	/* Number of steps to reach max_speed.
				 * The constructor must ensure that the
				 * value is  divisible by 2^STEPTIM. */
	uint8_t vref;		/* Vref (current limiter) setting. */
	unsigned int t_max;	/* Sum of a[0] to a[alen - 1] */
	uint16_t a[MTRTBL_SIZE];/* The acceleration table */
};

void build_motor_table(struct gl843_motor_setting *m,
			enum motor_step step,
			uint16_t min_speed,
			uint16_t max_speed,
			uint8_t vref);

int setup_scanning_profile(struct gl843_device *dev,
			   float y_start,
			   unsigned int lincnt,
			   int y_dpi,
			   enum motor_step type,
			   int fwdstep,
			   unsigned int exposure);

int do_move_test(struct gl843_device *dev);

#endif /* _GL843_MOTOR_H_ */
