/*
 *
 * Copyright (C) 2009 Andreas Robinson <andr345 at gmail dot com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "gl843_util.h"
#include "gl843_low.h"
#include "gl843_motor.h"

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
  |  \ <- DECSEL,                                       FMOVNO -> /
  |   \   FMOVDEC or FMOVNO (see LONGCURV)                       /
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
LONGCURV: If 0, FMOVNO and table 4 control deceleration for auto-go-home.
          If 1, FMOVDEC, and table 5 control deceleration for auto-go-home.
          See "Wall-hitting protection" in GL843 datasheet.
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
STEPSEL: Motor step type for tables 1, 2 and 3
FSTPSEL: Motor step type for tables 4 and 5

TODO: When are DECSEL and FMOVEDEC used?
 It seems unlikely that both are used at the same time as they define
 the same thing. Right?

Other bits:
TB3TB1:   When set, table 1 replaces table 3
TB5TB1:   When set, table 2 replaces table 5
MULSTOP:  STOPTIM multiplier.
*/


void cs4400f_build_motor_table(struct gl843_motor_setting *m,
	unsigned int speed, enum motor_step step);
void cs4400f_get_fast_feed_motor_table(struct gl843_motor_setting *m);


/* Set up the scanning envelope for the CS4400F
 *
 * y_start: scanning start [inches]
 * y_end:   scanning end [inches]
 * type:    step type
 * speed:   scanning speed [pixel clock ticks per step]
 * fwdstep: scan at most fwdstep steps, then backtrack. 0 = disable
 * exposure: LPERIOD * 2^TGTIME, for calculating Z1MOD and Z2MOD
 */
int setup_scanning_profile(struct gl843_device *dev,
			   float y_start,
			   float y_end,
			   int y_dpi,
			   enum motor_step type,
			   int fwdstep,
			   unsigned int exposure)
{
	struct gl843_motor_setting move;
	struct gl843_motor_setting scan;

	float Ks;	/* Number of scanning steps per inch */
	float Km;	/* Number of moving steps per inch */
	float Rms;	/* Km/Ks ratio */

	unsigned int speed;
	int scanfeed, feedl, lincnt;
	unsigned int z1mod, z2mod;

	speed = exposure * y_dpi / (dev->base_ydpi << type);

	DBG(DBG_info, "y_start = %f, y_end = %f, type = %d, speed = %u, "
		"fwdstep = %d, exposure = %u\n",
		y_start, y_end, type, speed, fwdstep, exposure);

	/*
	 * Set up motor acceleration
	 */

	cs4400f_get_fast_feed_motor_table(&move);
	cs4400f_build_motor_table(&scan, speed, type);

	DBG(DBG_info, "scan.alen = %d, move.alen = %d\n", scan.alen, move.alen);

	struct regset_ent motor1[] = {
		/* Misc */
		{ GL843_STEPTIM, STEPTIM },
		{ GL843_MULSTOP, 0 },
		{ GL843_STOPTIM, 15 }, /* or 15. TODO: When? */
		/* Scanning (table 1 and 3)*/
		{ GL843_STEPSEL, scan.type },
		{ GL843_STEPNO, scan.alen >> STEPTIM },
		{ GL843_FSHDEC, scan.alen >> STEPTIM },
		{ GL843_VRSCAN, scan.vref },
		/* Backtracking (table 2) - use scanning table for now */
		{ GL843_FASTNO, scan.alen >> STEPTIM },
		{ GL843_VRBACK, scan.vref },
		/* Fast feeding (table 4) and go-home (table 5) */
		{ GL843_FSTPSEL, move.type },
		{ GL843_FMOVNO, move.alen >> STEPTIM },
		{ GL843_FMOVDEC, move.alen >> STEPTIM },
		{ GL843_DECSEL, 1 }, /* Windows driver: 0 or 1 */
		{ GL843_VRMOVE, move.vref },
		{ GL843_VRHOME, move.vref },
	};
	set_regs(dev, motor1, ARRAY_SIZE(motor1));

	/*
	 * Set up moving distances
	 */

	Ks = dev->base_ydpi << scan.type;
	Km = dev->base_ydpi << move.type;
	Rms = Km / Ks;

	scanfeed = 1020; /* Max value minimizes carriage vibration at scan start */
	feedl = ((int) (Km * y_start + 0.5)) - 2 * move.alen;
	feedl = feedl - Rms * (scan.alen + scanfeed);

	if (feedl > 0) {
		/* Use fast moving */
		set_reg(dev, GL843_FASTFED, 1);
		set_reg(dev, GL843_SCANFED, scanfeed >> STEPTIM);
		set_reg(dev, GL843_FEEDL, feedl);
	} else {
		/* Don't use fast moving - no room to accelerate and decelerate. */
		feedl = (int) (Ks * y_start + 0.5) - scan.alen;
		if (feedl < 1) {
			/* No room to accelerate */
			feedl = 1;
			DBG(DBG_error, "Scan start @ %f mm, is too close to "
				"the home position. Minimum is %f mm at "
				"current resolution and scanning speed.\n",
				25.4 * y_start,
				25.4 * (scan.alen + feedl) / Ks);
			return -EINVAL;
		}
		set_reg(dev, GL843_FASTFED, 0);
		set_reg(dev, GL843_FEEDL, feedl);
	}

	lincnt = ((int) (Ks * (y_end - y_start) + 0.5));
	if (lincnt < 1) {
		DBG(DBG_warn, "Start and end positions are the same.\n");
		lincnt = 1;
	}
	set_reg(dev, GL843_LINCNT, lincnt);
	printf("feedl = %d, lincnt = %d\n", feedl, lincnt);

	/* TODO. */
	if (fwdstep > 0) {
		if (fwdstep != STEPTIM_ALIGN_DN(fwdstep)) {
			DBG(DBG_error, "fwdstep is not divisible by 2^STEPTIM\n");
			return -EINVAL;
		}
		set_reg(dev, GL843_FWDSTEP, fwdstep >> STEPTIM);
		/* FIXME: bwdstep should probably be less than fwdstep */
		set_reg(dev, GL843_BWDSTEP, fwdstep >> STEPTIM);
		set_reg(dev, GL843_ACDCDIS, 0);
	} else
		set_reg(dev, GL843_ACDCDIS, 1); /* Disable backtracking. */

	/*
	 * Set up sensor and motor timing relationships
	 */

	/* "scan" refers to table 1 */
	z2mod = (scan.t_max + scan.a[scan.alen - 1] * scanfeed) % exposure;
	/* "scan" refers to table 2 */
	z1mod = (scan.t_max + scan.a[scan.alen - 1] * fwdstep) % exposure;

	// TEST
	z1mod = 0;
	z2mod = 0;

	set_reg(dev, GL843_Z1MOD, z1mod);
	set_reg(dev, GL843_Z2MOD, z2mod);

	if (flush_regs(dev) < 0)
		return -EIO;

	send_motor_table(dev, 1, 1020, scan.a);
	send_motor_table(dev, 2, 1020, scan.a);
	send_motor_table(dev, 3, 1020, scan.a);
	send_motor_table(dev, 4, 1020, move.a);
	send_motor_table(dev, 5, 1020, move.a);

	return 0;
}

#if 0

/* GL843_FEDCNT == step counter. Unit: 1/8th step, i.e 1 step <=> FEDCNT = 8 */

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
	send_motor_table(dev, 1, 1020, m.tbl);
	printf("real length = %d, stepcnt = %d\n", m.len, m.stepcnt);

	/* Clear FEDCNT */
	set_reg(dev, GL843_CLRMCNT, 1);
	flush_regs(dev);

	/* Move forward (defined by FEEDL below) */

	set_reg(dev, GL843_STEPNO, m.stepcnt);
	set_reg(dev, GL843_STEPTIM, STEPTIM);
	set_reg(dev, GL843_VRMOVE, m.vref);

	set_reg(dev, GL843_FEEDL, 500 * (1 << m.step));
	set_reg(dev, GL843_STEPSEL, m.step);

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

#endif
