/*
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
#include <math.h>
#include "low.h"
#include "cs4400f.h"
#include "image.h"
#include "motor.h"
#include "util.h"

/* Move the scanner carriage without scanning.
 *
 * This function move the scanner head to a calibration or
 * lamp warmup position, or back home once completed.
 *
 * d: moving distance in inches. > 0: forward, < 0: back
 *    WARNING: A bad value for d can crash the carriage into the wall.
 */
int move_carriage(struct gl843_device *dev, float d)
{
	int ret;
	int feedl;
	struct gl843_motor_setting move;

	feedl = (int)(4800 * d + 0.5);
	if (feedl >= 0) {
		set_reg(dev, GL843_MTRREV, 0);
	} else {
		set_reg(dev, GL843_MTRREV, 1);
		feedl = -feedl;
	}

	cs4400f_build_motor_table(&move, 240, HALF_STEP);

	/* Subtract acceleration and deceleration distances */
	feedl = feedl - 2 * move.alen;
	if (feedl < 0) {
		/* The acceleration/deceleration curves are longer than
		 * the distance we wish to move. Set feedl = 0 and trim
		 * the curves to desired lengths. */
		move.alen = (feedl + 2 * move.alen) / 2;
		feedl = 0;
	}

	struct regset_ent motor1[] = {
		/* Misc */
		{ GL843_STEPTIM, STEPTIM },
		{ GL843_MULSTOP, 0 },
		{ GL843_STOPTIM, 0 },
		/* Scanning (table 1 and 3)*/
		{ GL843_STEPSEL, move.type },
		{ GL843_STEPNO, 1 },
		{ GL843_FSHDEC, 1 },
		{ GL843_VRSCAN, 3 },	// FIXME: Not hard coded ...
		/* Backtracking (table 2) */
		{ GL843_FASTNO, 1 },
		{ GL843_VRBACK, 3 },	// FIXME: Not hard coded ...
		/* Fast feeding (table 4) and go-home (table 5) */
		{ GL843_FSTPSEL, move.type },
		{ GL843_FMOVNO, move.alen >> STEPTIM },
		{ GL843_FMOVDEC, move.alen >> STEPTIM },
		{ GL843_DECSEL, 1 }, /* Windows driver: 0 or 1 */
		{ GL843_VRMOVE, 5 },	//move.vref FIXME: Not hard coded...
		{ GL843_VRHOME, 5 },	//move.vref FIXME: Not hard coded...

		{ GL843_FASTFED, 1 },
		{ GL843_SCANFED, 0 },
		{ GL843_FEEDL, feedl },
		{ GL843_LINCNT, 0 },
		{ GL843_ACDCDIS, 1 }, /* Disable backtracking. */

		{ GL843_Z1MOD, 0 },
		{ GL843_Z2MOD, 0 },
	};
	CHK(write_regs(dev, motor1, ARRAY_SIZE(motor1)));

	send_motor_table(dev, 1, move.a, 1020);
	send_motor_table(dev, 2, move.a, 1020);
	send_motor_table(dev, 3, move.a, 1020);
	send_motor_table(dev, 4, move.a, 1020);
	send_motor_table(dev, 5, move.a, 1020);

	set_reg(dev, GL843_NOTHOME, 0);
	set_reg(dev, GL843_AGOHOME, 1);
	set_reg(dev, GL843_MTRPWR, 1);
	CHK(flush_regs(dev));

	write_reg(dev, GL843_SCAN, 0);
	write_reg(dev, GL843_MOVE, 16);

	ret = 0;
chk_failed:
	return ret;
}

/* Saturate value v */
static float satf(float v, float min, float max)
{
	if (v < min)
		v = min;
	else if (v > max)
		v = max;
	return v;
}

/* AFE-specific, maximum gain as per the data sheet */
static float __attribute__ ((pure)) max_gain()
{
	return 7.428;
}

/* AFE-specific, minimum gain as per the data sheet */
static float __attribute__ ((pure)) min_gain()
{
	return 0.735;
}

/* AFE-specific: convert AFE gain to AFE register value. */
static int gain2val(float g)
{
	g = satf(g, min_gain(), max_gain());
	return (int) (283 - 208/g + 0.5);
}

static int scan_img(struct gl843_device *dev,
		    uint8_t *buf,
		    size_t stride, size_t height, int bpp,
		    int timeout)
{
	int ret, i;

	CHK(write_reg(dev, GL843_LINCNT, height));
	CHK(write_reg(dev, GL843_SCAN, 1));
	CHK(write_reg(dev, GL843_MOVE, 255));

	while (read_reg(dev, GL843_BUFEMPTY));

	for (i = 0; i < height; i++) {
		CHK(read_line(dev, buf, stride, bpp, timeout));
		buf += stride;
	}

	CHK(write_reg(dev, GL843_SCAN, 0));
	CHK(write_reg(dev, GL843_CLRLNCNT, 1));
	ret = 0;
chk_failed:
	return ret;
}

struct img_stat
{
	int min, max;
	float avg;
};

static void add_sample(struct img_stat *stat, int c)
{
	stat->min = (c < stat->min) ? c : stat->min;
	stat->max = (c > stat->max) ? c : stat->max;
	stat->avg += c;
}

static void clear_stat(struct img_stat *stat)
{
	stat->min = 65535;
	stat->max = 0;
	stat->avg = 0;
}

static void get_image_stats(struct gl843_image *img,
			    struct img_stat *r_stat,
			    struct img_stat *g_stat,
			    struct img_stat *b_stat)
{
	int bpp, n, m, i;

	if (img->bpp != 48) {
		DBG(DBG_error, "img->bpp != 48 (PIXFMT_RGB16)\n");
		return;
	}

	/* Byte count. Ignoring the last line; it can have bad pixels. */
	n = img->stride * (img->height-1);
	/* Pixel count. */
	m = img->width * (img->height-1);

	/* Calculate R, G and B component minimum, maximum and average. */

	clear_stat(r_stat);
	clear_stat(g_stat);
	clear_stat(b_stat);

	for (i = 0; i < n; i += 6) {
		uint16_t *p = (uint16_t *)(img->data + i);
		add_sample(r_stat, *p++);
		add_sample(g_stat, *p++);
		add_sample(b_stat, *p++);
	}
	r_stat->avg = r_stat->avg / m;
	g_stat->avg = g_stat->avg / m;
	b_stat->avg = b_stat->avg / m;

	DBG(DBG_info, "R(min,max,avg) = %d, %d, %.2f\n",
		r_stat->min, r_stat->max, r_stat->avg);
	DBG(DBG_info, "G(min,max,avg) = %d, %d, %.2f\n",
		g_stat->min, g_stat->max, g_stat->avg);
	DBG(DBG_info, "B(min,max,avg) = %d, %d, %.2f\n",
		b_stat->min, b_stat->max, b_stat->avg);
}

/*
 * Adjust the AFE offset.
 *
 * low:  known-good AFE offset-register value. See note.
 * high: known-good AFE offset-register value. See note. 
 *
 * Note:
 * 'low' and 'high' depend on the scanner model.
 * Choose them so that
 * 1. both produce black pixel samples > 0, and never zero.
 * 2. 'low' produces a a darker black level than 'high',
 * 3. 'low' and 'high' values are as far apart as possible,
 *    for better precision.
 * Example: For CanoScan 4400F, select low = 75, high = 0.
 * Notice that the 'low' register value is not necessarily
 * less than the 'high' register value. It is what comes
 * out of the scanner that counts.
 */
int find_blacklevel(struct gl843_device *dev,
		    struct gl843_image *img,
		    uint8_t low, uint8_t high)
{
	int ret;
	int g = gain2val(1.0);
	struct img_stat rlo_stat, rhi_stat;
	struct img_stat glo_stat, ghi_stat;
	struct img_stat blo_stat, bhi_stat;
	double rk, gk, bk, rm, gm, bm;
	int ro, go, bo;

	/* Scan with the lamp off to produce black pixels. */
	CHK(set_lamp(dev, LAMP_OFF, 0));

	/* Set gain = 1.0 */

	CHK(write_afe(dev, 40, g));
	CHK(write_afe(dev, 41, g));
	CHK(write_afe(dev, 42, g));

	/* Sample 'low' black level */

	CHK(write_afe(dev, 32, low));
	CHK(write_afe(dev, 33, low));
	CHK(write_afe(dev, 34, low));

	CHK(scan_img(dev, img->data,
		img->stride, img->height, img->bpp, 10000));

	get_image_stats(img, &rlo_stat, &glo_stat, &blo_stat);

	/* Sample 'high' black level */

	CHK(write_afe(dev, 32, high));
	CHK(write_afe(dev, 33, high));
	CHK(write_afe(dev, 34, high));

	CHK(scan_img(dev, img->data,
		img->stride, img->height, img->bpp, 10000));

	get_image_stats(img, &rhi_stat, &ghi_stat, &bhi_stat);

	/* Use the line eqation to find the best offset value */

	rk = (double) (rhi_stat.avg - rlo_stat.avg) / (high - low);
	gk = (double) (ghi_stat.avg - glo_stat.avg) / (high - low);
	bk = (double) (bhi_stat.avg - blo_stat.avg) / (high - low);

	rm = rlo_stat.avg - rk * low;
	gm = glo_stat.avg - bk * low;
	bm = blo_stat.avg - gk * low;

	/* TODO: range checking */

	ro = (int)(-rm / rk + 0.5);
	go = (int)(-gm / gk + 0.5);
	bo = (int)(-bm / bk + 0.5);

	CHK(write_afe(dev, 32, ro));
	CHK(write_afe(dev, 33, go));
	CHK(write_afe(dev, 34, bo));

	DBG(DBG_info, "RGB offset values: %d, %d, %d\n", ro, go, bo);
#if 0
	/* Debug: Test the result: */
	CHK(scan_img(dev, img->data,
		img->stride, img->height, img->bpp, 10000));
	get_image_stats(img, &rlo_stat, &glo_stat, &blo_stat);
#endif
	ret = 0;
chk_failed:
	return ret;	
}


int warm_up_lamp(struct gl843_device *dev,
		 struct gl843_image *img,
		 enum gl843_lamp source,
		 int timeout /* TODO */)
{
	int ret;

	struct img_stat r_stat = {};
	struct img_stat g_stat = {};
	struct img_stat b_stat = {};

	int gval = gain2val(min_gain());

	CHK(write_afe(dev, 40, gval));
	CHK(write_afe(dev, 41, gval));
	CHK(write_afe(dev, 42, gval));

	CHK(set_lamp(dev, source, 4));

	while (1) {
		float r0, g0, b0;	/* Previous averages */

		r0 = r_stat.avg;
		g0 = g_stat.avg;
		b0 = b_stat.avg;

		CHK(scan_img(dev, img->data,
			img->stride, img->height, img->bpp, 10000));
		get_image_stats(img, &r_stat, &g_stat, &b_stat);

		DBG(DBG_info, "delta RGB average: %.2f, %.2f, %.2f\n",
			r_stat.avg - r0, g_stat.avg - g0, b_stat.avg - b0);

		if (abs(r_stat.avg - r0) < 10 &&
		    abs(g_stat.avg - g0) < 10 &&
		    abs(b_stat.avg - b0) < 10) {
			break;
		}
		usleep(500000);
	}
	ret = 0;
chk_failed:
	return ret;	
}

/*
 * Calculate new gain
 *
 * curr: sampled max value at current gain
 * tgt:  target max value at new gain, must be 1 - 65535.
 * g:    current gain
 */
float calc_gain(int curr, int tgt, float g)
{
	g = satf(g, min_gain(), max_gain());
	tgt = (tgt < 1) ? 1 : tgt;
	curr = (curr < 1) ? 1 : curr;
	return satf(g * tgt / curr, min_gain(), max_gain());
}

/* Adjust the AFE gain */

int set_gain(struct gl843_device *dev,
	     struct gl843_image *img,
	     enum gl843_lamp source)
{
	int ret;

	float rg, gg, bg;
	int rval, gval, bval, prev_rval, prev_gval, prev_bval;

	struct img_stat r_stat = {};
	struct img_stat g_stat = {};
	struct img_stat b_stat = {};

	float target = 65535 * 0.98;

	CHK(set_lamp(dev, source, 4));

	int done = 0;

	while (done < 8) {
		CHK(write_afe(dev, 40, gain2val(rg)));
		CHK(write_afe(dev, 41, gain2val(gg)));
		CHK(write_afe(dev, 42, gain2val(bg)));

		CHK(scan_img(dev, img->data,
			img->stride, img->height, img->bpp, 10000));

		get_image_stats(img, &r_stat, &g_stat, &b_stat);

		rg = 1/satf(r_stat.max / (rg * target), 1/max_gain(), 1/min_gain());
		gg = 1/satf(g_stat.max / (gg * target), 1/max_gain(), 1/min_gain());
		bg = 1/satf(b_stat.max / (bg * target), 1/max_gain(), 1/min_gain());

		prev_rval = rval;
		prev_gval = gval;
		prev_bval = bval;

		rval = gain2val(rg);
		gval = gain2val(gg);
		bval = gain2val(bg);

		if (abs(rval - prev_rval) <= 1 &&
		    abs(gval - prev_gval) <= 1 &&
		    abs(bval - prev_bval) <= 1) {
			done++;
			printf("done = %d\n", done);
		} else {
			done = 0;
		}

		DBG(DBG_info, "Gain (RGB) = %.2f, %.2f, %.2f\n", rg, gg, bg);
		DBG(DBG_info, "RGB gain values: %d, %d, %d\n",
			gain2val(rg), gain2val(gg), gain2val(bg));

		usleep(500000);
	}


	ret = 0;
chk_failed:
	return ret;	
}

int do_warmup_scan(struct gl843_device *dev, float y_pos)
{
	int ret;

	int width = 2552;
	int height = 10;
	int dpi = 1200;

	enum gl843_pixformat fmt = PXFMT_RGB16;
	struct gl843_image *img;

	img = create_image(width, height, fmt);

	CHK(write_reg(dev, GL843_SCANRESET, 1));
	while(!read_reg(dev, GL843_HOMESNR))
		usleep(10000);
	CHK(do_base_configuration(dev));
	CHK(move_carriage(dev, y_pos));
	while (read_reg(dev, GL843_MOTORENB))
		usleep(10000);

	CHK(setup_ccd_and_afe(dev,
			/* fmt */ fmt,
			/* start_x */ 128,
			/* width */ width * 4800 / dpi,
			/* dpi */ dpi,
			/* afe_dpi */ dpi,
			/* linesel */ 0,
		  	/* tgtime */ 0,
			/* lperiod */ 11640,
			/* expr,g,b */ 40000, 40000, 40000));

	CHK(set_lamp(dev, LAMP_OFF, 0));
	//CHK(set_lamp(dev, LAMP_PLATEN, 4));

	set_postprocessing(dev);
	CHK(flush_regs(dev));

	set_reg(dev, GL843_MTRREV, 0);
	set_reg(dev, GL843_NOTHOME, 0);
	set_reg(dev, GL843_CLRLNCNT, 1);
	set_reg(dev, GL843_MTRPWR, 0);
	set_reg(dev, GL843_AGOHOME, 0);
	CHK(flush_regs(dev));

	CHK(write_afe(dev, 4, 0));

	CHK(write_afe(dev, 1, 0x23));
	CHK(write_afe(dev, 2, 0x24));
	CHK(write_afe(dev, 3, 0x2f)); /* Can be 0x1f or 0x2f */

	CHK(find_blacklevel(dev, img, 75, 0));

	CHK(warm_up_lamp(dev, img, LAMP_PLATEN, 0));
#if 0

	int i,j,k;

	for (j = 0; j < 10; j++) {
		int offset = j*10*img->stride;

		CHK(set_afe_offsets_and_gains(dev,
			130, 128, 0,
			j*25, 96, 96));

		CHK(scan_img(dev, img->data + offset,
			img->stride, 10, img->bpp, 10000));

		int rmin = 65535, gmin = 65535, bmin = 65535;
		int ravg = 0, gavg = 0, bavg = 0;
		int rmax = 0, gmax = 0, bmax = 0;
		uint16_t *p = (uint16_t *) (img->data + offset);
		for (i = 0, k = 1; i < img->stride * 9; i += 6, k++) {
			rmin = (*p < rmin) ? *p : rmin;
			rmax = (*p > rmax) ? *p : rmax;
			ravg += *p;
			p++;
			gmin = (*p < gmin) ? *p : gmin;
			gmax = (*p > gmax) ? *p : gmax;
			gavg += *p;
			p++;
			bmin = (*p < bmin) ? *p : bmin;
			bmax = (*p > bmax) ? *p : bmax;
			bavg += *p;
			p++;
		}
		ravg = ravg / k;
		gavg = gavg / k;
		bavg = bavg / k;

		printf("min = %d,%d,%d, max = %d,%d,%d, avg = %d, %d, %d\n",
			rmin, gmin, bmin, rmax, gmax, bmax,
			ravg, gavg, bavg);
	}
	write_image("test.pnm", img);

#endif

	destroy_image(img);

	CHK(move_carriage(dev, -y_pos));
	while (!read_reg(dev, GL843_HOMESNR))
		usleep(10000);
	CHK(write_reg(dev, GL843_MTRPWR, 0));

	ret = 0;
chk_failed:
	return ret;
}
