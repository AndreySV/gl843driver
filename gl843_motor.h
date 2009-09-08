#ifndef _GL843_MOTOR_H_
#define _GL843_MOTOR_H_

enum motor_step
{
	FULL_STEP = 0,
	HALF_STEP = 1,
	QUARTER_STEP = 2,
	EIGHTH_STEP = 3
};


#define MTRTBL_SIZE 1024
struct gl843_motor_accel
{
	uint8_t vref;		/* Vref (current limiter) setting */
	enum motor_step step;	/* Step size */

	uint16_t min_speed;	/* Start speed [counter ticks per step] */
	uint16_t max_speed;	/* End speed [counter ticks per step] */
	size_t len;		/* Maximum number of steps in table
				 * (on GL843, always len = 1020) */
	size_t steps_max;	/* Index in tbl where max_speed is reached
				 * If == 0: the target speed is not reached. */
	unsigned int t_max;	/* Sum of counter ticks from tbl[0] to
				 * tbl[max_steps], inclusive.
				 * If steps_max == 0: t_max is the sum from
				 * tbl[0] to tbl[len-1], inclusive. */
	uint16_t tbl[MTRTBL_SIZE];	/* The acceleration table */
};

size_t build_motor_table(struct gl843_motor_accel *m,
			 enum motor_step step,
			 uint16_t min_speed,
			 uint16_t max_speed,
			 uint8_t vref);

#endif /* _GL843_MOTOR_H_ */
