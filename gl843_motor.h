#ifndef _GL843_MOTOR_H_
#define _GL843_MOTOR_H_

enum motor_step
{
	FULL_STEP = 0,
	HALF_STEP = 1,
	QUARTER_STEP = 2,
	EIGHTH_STEP = 3
};

/* Must be 1020 (hardware limit). */
#define MTRTBL_SIZE 1020
/* This must be 2. See build_motor_table() */
#define STEPTIM_VAL 2

struct gl843_motor_setting
{
	uint8_t vref;		/* Vref (current limiter) setting */
	enum motor_step step;	/* Step size */
	uint16_t min_speed;	/* Start speed [counter ticks per step] */
	uint16_t max_speed;	/* End speed [counter ticks per step] */
	unsigned int len;	/* Number of steps to reach max_speed */
	unsigned int stepcnt;	/* Number of steps divided by 2^STEPTIM_VAL */
	unsigned int t_max;	/* Sum of counter ticks from tbl[0] to
				 * tbl[stepcnt*(1 << STEPTIM_VAL) - 1] */
	uint16_t tbl[MTRTBL_SIZE];	/* The acceleration table */
};

void build_motor_table(struct gl843_motor_setting *m,
			enum motor_step step,
			uint16_t min_speed,
			uint16_t max_speed,
			uint8_t vref);

int do_move_test(struct gl843_device *dev);

#endif /* _GL843_MOTOR_H_ */
