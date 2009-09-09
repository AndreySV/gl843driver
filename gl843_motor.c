#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "gl843_util.h"
#include "gl843_low.h"
#include "gl843_motor.h"

/* GL843_FEDCNT == step counter. Unit: 1/8th step, i.e 1 step <=> FEDCNT = 8 */

int send_motor_accel(struct gl843_device *dev,
		     int table, size_t len, uint16_t *a)
{
	uint16_t buf[len];
	uint16_t *p;

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
#if 0
	printf("\nTable %d: %d steps\n\n", table, (int) len);
	int i;
	for (i = 0; i < len; i++) {
		printf("%d ", p[i]);
	}
	printf("\n");
#endif
	return xfer_bulk(dev, (uint8_t *) p,
		len * sizeof(uint16_t), (table-1) * 2048, MOTOR_SRAM | BULK_OUT);
}

extern int usleep();

void cs4400f_build_motor_table(struct gl843_motor_setting *m,
	unsigned int speed, enum motor_step step);

/* Use this function to explore the motor settings of your scanner. */
int do_move_test(struct gl843_device *dev)
{
	struct gl843_motor_setting m;
	struct dbg_timer tmr;

	init_timer(&tmr, CLOCK_REALTIME);

/*116000*/
	//build_motor_table(&m, FULL_STEP, 7000, 370, 0);
	//build_motor_table(&m, HALF_STEP, 11000, 4500, 0);

//#define RESTORE_MOTOR_POS
#ifdef RESTORE_MOTOR_POS
	cs4400f_build_motor_table(&m, 175, HALF_STEP);
#else
	int speed = 100;
	int step = QUARTER_STEP;
	cs4400f_build_motor_table(&m, speed, step);
#endif
	send_motor_accel(dev, 1, 1020, m.tbl);
	printf("real length = %d, stepcnt = %d\n", m.len, m.stepcnt);

	/* Clear FEDCNT */
	set_reg(dev, GL843_CLRMCNT, 1);
	flush_regs(dev);

	/* Move forward (defined by FEEDL below) */

	set_reg(dev, GL843_STEPNO, m.stepcnt);
	set_reg(dev, GL843_STEPTIM, STEPTIM_VAL);
	set_reg(dev, GL843_VRMOVE, m.vref);

	set_reg(dev, GL843_FEEDL, 500 * (1 << m.step));
	set_reg(dev, GL843_STEPSEL, m.step);
#if 0
	set_reg(dev, GL843_FSTPSEL, m.step);
	set_reg(dev, GL843_FASTNO, m.step);
	set_reg(dev, GL843_FSHDEC, m.step);
	set_reg(dev, GL843_FMOVNO, m.step);
	set_reg(dev, GL843_FMOVDEC, m.step);

	set_reg(dev, GL843_VRBACK, m.vref);
	set_reg(dev, GL843_VRSCAN, m.vref);
	set_reg(dev, GL843_VRHOME, m.vref);
#endif
	printf("vref = %d\n", m.vref);

	set_reg(dev, GL843_MTRREV, 0);
	set_reg(dev, GL843_MTRPWR, 1);
	flush_regs(dev);

	set_reg(dev, GL843_MOVE, 0);	/* Start moving */
	flush_regs(dev);

	int moving = 1;
	while (moving) {
		read_regs(dev, GL843_MOTORENB, GL843_FEDCNT, -1);
		moving = get_reg(dev, GL843_MOTORENB);
		printf("\rfedcnt = %d        ", get_reg(dev, GL843_FEDCNT));
		//fflush(stdout);
		usleep(1000);
	}
	printf("\n");

	printf("-----------\n");

	set_reg(dev, GL843_FEEDL, 500 * (1 << m.step));


	/* Back up again */
#ifndef RESTORE_MOTOR_POS
	set_reg(dev, GL843_MTRREV, 1);
	set_reg(dev, GL843_MOVE,0);
	flush_regs(dev);

	moving = 1;
	while (moving) {
		read_regs(dev, GL843_MOTORENB, GL843_FEDCNT, -1);
		moving = get_reg(dev, GL843_MOTORENB);
		printf("\rfedcnt = %d        ", get_reg(dev, GL843_FEDCNT));
		//fflush(stdout);
		usleep(1000);
	}
	printf("\n");
#endif
	set_reg(dev, GL843_MTRPWR, 0);
	set_reg(dev, GL843_FULLSTP, 1);
	flush_regs(dev);

	printf("elapsed time: %f [ms]\n", get_timer(&tmr));

	usleep(1000000);

	set_reg(dev, GL843_SCANRESET, 0);
	flush_regs(dev);

	return 0;
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
