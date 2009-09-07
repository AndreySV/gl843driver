#ifndef _GL843_MOTOR_H_
#define _GL843_MOTOR_H_

#define MTRTBL_SIZE 1024
struct gl843_motor_accel
{
	uint16_t min_speed;	/* Start speed [counter ticks per step] */
	uint16_t max_speed;	/* End speed [counter ticks per step] */
	size_t len;		/* Maximum number of steps in table */
	size_t steps_max;	/* Index in tbl where max_speed is reached
				 * 0: not reached */
	unsigned int t_max;	/* Sum of counter ticks from tbl[0] to
				 * tbl[max_steps], inclusive.
				 * If steps_max == 0, t_max is the sum from
				 * tbl[0] to tbl[len-1], inclusive. */
	uint16_t tbl[MTRTBL_SIZE];	/* The acceleration table */
};

/* m:		table to fill with data
 * min_speed:   timer count for first motor step
 *              I.e start speed AND start accel,
 *              since we're starting from a standstill.
 * max_speed:   target timer count (i.e target speed)
 * len:         table size (i.e number of steps), max 1020 (hw limit)
 *
 * Returns:     steps_max value of struct.
 *
 * Note:        min_speed > max_speed, since these perameters
 *              express speed as duration per step, not velocity.
 */
size_t build_motor_table(struct gl843_motor_accel *m,
			 uint16_t min_speed, uint16_t max_speed);

#endif /* _GL843_MOTOR_H_ */
