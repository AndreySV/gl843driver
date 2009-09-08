#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "gl843_util.h"
#include "gl843_low.h"
#include "gl843_motor.h"

/* m:		Empty table to fill with data
 * min_speed:   timer count for first motor step. That is, start speed AND
 *              start acceleration, since we're always starting from standstill.
 * max_speed:   target timer count (i.e target speed).
 *
 * Returns:     steps_max value of struct.
 *
 * Note:        Set min_speed >= max_speed. These perameters
 *              express speed as motor clock ticks per step, not velocity.
 */
size_t build_motor_table(struct gl843_motor_accel *m,
			 enum motor_step step,
			 uint16_t min_speed,
			 uint16_t max_speed,
			 uint8_t vref)
{
	/* Linear acceleration of a stepping motor
	 * TODO: with smooth transition from acceleration to constant speed.
	 *
	 * Reference: David Austin, Generate stepper-motor speed profiles
	 * in real time, Embedded Systems Programming, January 2005
	 */

	double c, c_min;
	unsigned int i, t;
	uint16_t *tbl;

	c = min_speed; /* min_speed is named c0 in the article */
	c_min = max_speed;

	m->step = step;
	m->vref = vref;
	m->min_speed = min_speed;
	m->max_speed = max_speed;
	m->len = MTRTBL_SIZE;
	m->steps_max = 0;
	m->t_max = 0;
	t = 0;
	tbl = m->tbl;

	/* Do two steps at lowest speed to
	 * get the motor running stably */

	*tbl = (int) (c + 0.5);
	t += *tbl++;
	*tbl = (int) (c + 0.5);
	t += *tbl++;

	/* Build table */

	for (i = 2; i < m->len; i++) {
		c -= 2*c / (4*i + 1.0);
		if (c <= c_min) {
			c = c_min;
			if (m->steps_max == 0) {
				m->steps_max = i;
				m->t_max = t + c;
			}
		}
		*tbl = (int) (c + 0.5);
		t += *tbl++;
	}
	if (m->steps_max == 0) {
		DBG(DBG_warn, "Warning: cannot reach target speed %d, "
			"when starting from %d [ticks per step]",
			max_speed, min_speed);
		m->t_max = t;
	}

	return m->steps_max;
}

/* GL843_FEDCNT == step counter. Unit: 1/8th step, i.e 1 step <=> FEDCNT = 8 */

int send_motor_accel(struct gl843_device *dev,
		     int table, size_t len, uint16_t *a)
{
	uint16_t buf[len];
	uint16_t *p;
	int ret;

	if (table < 1 || table > 5)
		return -1;

	if (host_is_big_endian()) {
		int i;
		for (i = 0; i < len; i++) {
			buf[i] = ((a[i] >> 8) & 0xff) | ((a[i] & 0xff) << 8);
		}
		p = buf;
	} else {/* if (host_is_little_endian()) */
		p = a;
	}
	return xfer_bulk(dev, (uint8_t *) p,
		len * sizeof(uint16_t), (table-1) * 2048, MOTOR_SRAM | BULK_OUT);
}

void write_safe_accel_tables(struct gl843_device *dev)
{
	int i;
	struct gl843_motor_accel m;

	//build_motor_table(&m, 3700, 175);
	build_motor_table(&m, FULL_STEP, 8800, 400, 1);
	for (i = 1; i < 6; i++) {
		send_motor_accel(dev, i, m.len, m.tbl);
	}
}

/* Ref: gl843 datasheet, FMOVNO register

Scanning with fast feed (FASTFED = 1)   Direction: ----->

     moving to scan start                         scanning

          FEEDL                       scan start            scan stop
speed       |                              .                     .
  |     ____v_____                 SCANFED .       LINCNT        .
  |    /          \                   |    .          |          .
  |   /            \              ____v____.__________v__________.
  |  / <- FMOVNO -> \  STOPTIM   /         .                      \
  | /                \    |     /<- STEPNO .             FSHDEC -> \
  |/__________________\___v____/___________.________________________\__ distance

Scanning without fast feed (FASTFED = 0)   Direction : ----->

                                      scan start            scan stop
speed                                      .                     .
  |                FEEDL                   .       LINCNT        .
  |                  |                     .          |          .
  |   _______________v_____________________.__________v__________.
  |  /                                     .                      \
  | / <- STEPNO                            .             FSHDEC -> \
  |/_______________________________________.________________________\__ distance

Moving home after scanning    Direction: <-----

speed
  |____________________________________________________________________ distance
  |\                                                                /
  | \                                                              /
  |  \ <- DECSEL, FMOVDEC                               FMOVNO -> /
  |   \                                                          /
  |    \________________________________________________________/


Scanner backtracking when the buffer is full

  |     scan start        buffer full
  |            .  FWDSTEP   .
  |            .      |     .
  |            .______v_____.       direction: ------>
  |           /              \
  |          / <-- STEPNO --> \
  |_________/__________________\______
  |         \                  /
  |          \  <- FASTNO ->  /
  |           \              /
  |            \  BWDSTEP   /       direction: <-----
  |             \    |     /
  |              \___v____/

STEPNO:  Number of acceleration steps before scanning,  in motor table 1.
FASTNO:  Number of acceleration steps in backtracking,  in motor table 2.
FSHDEC:  Number of deceleration steps after scanning,   in motor table 3.
FMOVNO:  Number of acceleration steps for fast feeding, in motor table 4.
FMOVDEC: Number of deceleration steps for auto-go-home, in motor table 5.
FEEDL:   Number of feeding steps.
SCANFED: Number of feeding steps before scanning.
LINCNT:  Number of lines (steps) to scan.
FWDSTEP:
BWDSTEP:
STEPTIM: Multiplier for STEPNO, FASTNO, FSHDEC, FMOVNO, FMOVDEC,
         SCANFED, FWDSTEP, and BWDSTEP
         The actual number of steps in each table or register is
         <step_count> * 2^STEPTIM.
DECSEL:  Number of deceleration steps after touching home sensor.
         (Actual number of steps is 2^DECSEL.)
STEPSEL: Motor step type (full, half, quarter, eighth) for tables 1, 2 and 3
FSTPSEL: Motor step type for tables 4 and 5

TODO: When are DECSEL and FMOVEDEC used?
 It seems unlikely that both are used at the same time as they define
 the same thing. Right?

Other bits:
TB3TB1:   When set, table 1 replaces table 3
TB5TB1:   When set, table 2 replaces table 5
MULSTOP:  STOPTIM multiplier.
*/
