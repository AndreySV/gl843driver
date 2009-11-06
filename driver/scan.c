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
static int move_carriage(struct gl843_device *dev, float d)
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

static int scan_img(struct gl843_device *dev,
		    struct gl843_image *img,
		    int timeout)
{
	int ret, i;
	uint8_t *buf;

	CHK(write_reg(dev, GL843_LINCNT, img->height));
	CHK(write_reg(dev, GL843_SCAN, 1));
	CHK(write_reg(dev, GL843_MOVE, 255));

	while (read_reg(dev, GL843_BUFEMPTY));

	buf = img->data;
	for (i = 0; i < img->height; i++) {
		CHK(read_line(dev, buf, img->stride, img->bpp, timeout));
		buf += img->stride;
	}

	CHK(write_reg(dev, GL843_SCAN, 0));
	CHK(write_reg(dev, GL843_CLRLNCNT, 1));
	ret = 0;
chk_failed:
	return ret;
}

/* Color-index-to-name, for debugging purposes */
static const char *__attribute__ ((pure)) idx_name(int i)
{
	switch (i) {
	case 0:
		return "red";
	case 1:
		return "green";
	case 2:
		return "blue";
	default:
		return "(unknown)";
	}
}

/* Saturate value v */
static float __attribute__ ((pure)) satf(float v, float min, float max)
{
	if (v < min)
		v = min;
	else if (v > max)
		v = max;
	return v;
}

/* AFE-specific, maximum gain as per the data sheet */
float __attribute__ ((pure)) max_afe_gain()
{
	return 7.428;
}

/* AFE-specific, minimum gain as per the data sheet */
float __attribute__ ((pure)) min_afe_gain()
{
	return 0.735;
}

/* AFE-specific: convert AFE gain to AFE register value. */
static int __attribute__ ((pure)) gain_to_val(float g)
{
	g = satf(g, min_afe_gain(), max_afe_gain());
	return (int) (283 - 208/g + 0.5);
}

static int write_afe_gain(struct gl843_device *dev, int i, float g)
{
	DBG(DBG_info, "%s gain = %.2f, val = %d\n",
		idx_name(i), g, gain_to_val(g));
	return write_afe(dev, 40 + i, gain_to_val(g));
}

struct img_stat
{
	int min[3], max[3];
	float avg[3];
};

/* Calculate R, G and B component minimum, maximum and average. */
static void get_image_stats(struct gl843_image *img,
			    struct img_stat *stat)
{
	int n, m;
	int i, j;

	if (img->bpp != 48) {
		DBG(DBG_error, "img->bpp != 48 (PXFMT_RGB16)\n");
		return;
	}

	/* Byte count. Ignoring the last line; it can have bad pixels. */
	n = img->stride * (img->height-1);
	/* Pixel count. */
	m = img->width * (img->height-1);

	for (i = 0; i < 3; i++) {
		stat->min[i] = 65535;
		stat->max[i] = 0;
		stat->avg[i] = 0;
	}
	for (j = 0; j < n; j += 6) {
		uint16_t *p = (uint16_t *)(img->data + j);
		for (i = 0; i < 3; i++) {
			int c = *p++;
			stat->min[i] = (c < stat->min[i]) ? c : stat->min[i];
			stat->max[i] = (c > stat->max[i]) ? c : stat->max[i];
			stat->avg[i] += c;
		}
	}

	for (i = 0; i < 3; i++) {
		stat->avg[i] = stat->avg[i] / m;
		DBG(DBG_info, "%s (min,max,avg) = %d, %d, %.2f\n",
			idx_name(i), stat->min[i], stat->max[i], stat->avg[i]);
	}
}

/* Calculate average pixel intensity of every column.
 * The result is stored in the first line of the image.
 * The image height must be > 2, and fmt = PXFMT_RGB16 
 */
static void get_vertical_average(struct gl843_image *img)
{
	int i, y, height, avg;
	uint16_t *p0, *p;

	if (img->bpp != 48) {
		DBG(DBG_error, "img->bpp != 48 (PXFMT_RGB16)\n");
		return;
	}
	if (img->height < 2) {
		DBG(DBG_error, "img->height < 2\n");
		return;
	}

	height = img->height - 1; /* Ignoring last line, may have bad pixels */

	for (i = 0; i < img->stride; i += 2) {
		p0 = (uint16_t *)(img->data + i);

		/* Get average of all pixels in current column */

		avg = 0;
		p = p0;
		for (y = 0; y < height; y++) {
			avg += *p;
			p += img->stride / 2;
		}
		avg = avg / height;
		*p0 = avg;
	}
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
	int ret, i;
	struct img_stat lo_stat, hi_stat;

	DBG(DBG_msg, "Calibrating A/D-converter black level.\n");

	/* Scan with the lamp off to produce black pixels. */
	CHK(set_lamp(dev, LAMP_OFF, 0));

	/* Sample 'low' black level */

	for (i = 0; i < 3; i++) {
		CHK(write_afe_gain(dev, i, 1.0));
		CHK(write_afe(dev, 32 + i, low));  /* Set 'low' black level */
	}
	CHK(scan_img(dev, img, 10000));
	get_image_stats(img, &lo_stat);

	/* Sample 'high' black level */

	for (i = 0; i < 3; i++) {
		CHK(write_afe(dev, 32 + i, high)); /* Set 'high' black level */
	}
	CHK(scan_img(dev, img, 10000));
	get_image_stats(img, &hi_stat);

	/* Use the line eqation to find and set the best offset value */

	for (i = 0; i < 3; i++) {
		double m, c; /* y = mx + c */
		int o;	/* offset */
		m = (hi_stat.avg[i] - lo_stat.avg[i]) / (high - low);
		c = lo_stat.avg[i] - m * low;
		/* TODO: range checking */
		o = (int) (-c / m + 0.5);
		CHK(write_afe(dev, 32 + i, o));
		DBG(DBG_info, "AFE %s offset = %d\n", idx_name(i), o);
	}

	/* Debug: Test the result: */
	CHK(scan_img(dev, img, 10000));
	get_image_stats(img, &lo_stat);

	ret = 0;
chk_failed:
	return ret;	
}

/* Get progress, in percent, of lamp warmup */
static float get_progress(float dL_start, float dL_end, float dL_prev, float dL)
{
	float span, progress, prev_progress;

	/* Linearize and scale dL and dL_prev to the range [0.0, 100.0] */

	span = logf(dL_start) - logf(dL_end);
	prev_progress = ((logf(dL_start) - logf(dL_prev)) / span) * 100;
	progress = ((logf(dL_start) - logf(dL)) / span) * 100;

	/* Don't let progress go backwards ... */
	if (prev_progress > progress)
		progress = prev_progress;

	return satf(progress, 0, 100);
}

/* Wait until the lamp has warmed up.
 * Note: Don't forget to turn it on first ...
 */
int warm_up_lamp(struct gl843_device *dev,
		 struct gl843_image *img)
{
	int ret, i;
	int n;			/* Number of scans */
	struct img_stat s;

	float L = 0, L_prev;	/* Lamp intensity */
	float dL = -1, dL_prev;	/* Lamp intensity delta */
	float dL_start = -1;	/* Inital intensity delta */
	float dL_end = 50;	/* Target intensity delta */

	DBG(DBG_msg, "Warming up lamp.\n");

	for (i = 0; i < 3; i++)
		CHK(write_afe_gain(dev, i, min_afe_gain()));

	n = 0;
	while (1) {
		/* Scan and get lamp intensity, L and intensity delta, dL.
		 * Here, L is defined as the average of all subpixels in
		 * the scanned image. */
		L_prev = L;
		CHK(scan_img(dev, img, 10000));
		get_image_stats(img, &s);
		L = 0;
		for (i = 0; i < 3; i++)
			L += s.avg[i] / 3;

		dL_prev = dL;
		dL = abs(L - L_prev);

		if (n == 1) {
			dL_start = dL;
		} else if (n > 1) {
			float p;

			p = get_progress(dL_start, dL_end, dL_prev, dL);

			DBG(DBG_info, "L = %.2f, dL = %.2f\n", L, dL);
			DBG(DBG_msg, "  progress: %.0f%%\n", p);

			/* The lamp is warm when dL is small enough */
			if (dL < dL_end)
				break;
		}
		usleep(500000);
		n++;
	}
	ret = 0;
chk_failed:
	return ret;	
}

/* Adjust the AFE gain */
int set_gain(struct gl843_device *dev,
	     struct gl843_image *img)
{
	int ret, i;
	float g[3];
	struct img_stat stat;
	/* Target at 95% of max allows slight increase in lamp brightness. */
	const float target = 65535 * 0.95;

	DBG(DBG_msg, "Calibrating A/D-converter gain.\n");

	/* Scan at minimum gain */
	for (i = 0; i < 3; i++) {
		g[i] = min_afe_gain();
		CHK(write_afe_gain(dev, i, g[i]));
	}
	CHK(scan_img(dev, img, 10000));
	get_image_stats(img, &stat);

	/* Calculate and set gain that enables full dynamic range of AFE. */
	for (i = 0; i < 3; i++) {
		if (stat.max[i] < 1)
			stat.max[i] = 1;	/* avoid div-by-zero */
		g[i] = g[i] * target / stat.max[i];
		CHK(write_afe_gain(dev, i, g[i]));
	}

	ret = 0;
chk_failed:
	return ret;	
}

int calculate_shading(struct gl843_device *dev,
		      int width, int height, uint16_t A,
		      uint16_t **buf, size_t *len)
{
	int ret, i;
	struct gl843_image *light_img = NULL, *dark_img = NULL;
	uint16_t *Ln, *Dn, *p;
	const int target = 0x3fff;
	int div_by_zero = 0;
	int gain_overflow = 0;

	*buf = NULL;
	CHK_MEM(light_img = create_image(width, height, PXFMT_RGB16));
	CHK_MEM(dark_img = create_image(width, height, PXFMT_RGB16));
	*len = (light_img->stride+8000) * 2;
	CHK_MEM(*buf = malloc(*len));

	/* Scan light (white) pixels */

	/* Assume lamp is on */
	CHK(scan_img(dev, light_img, 10000));
	//get_vertical_average(light_img);
	write_image("light.pnm", light_img);

	/* Scan dark (black) pixels */

	CHK(set_lamp(dev, LAMP_OFF, 0));
	CHK(scan_img(dev, dark_img, 10000));
	//get_vertical_average(dark_img);
	write_image("dark.pnm", dark_img);

	/* Calculate shading
	 * Ref: shading & correction in GL843 datasheet. */

	p = *buf;
	Ln = (uint16_t *) light_img->data;
	Dn = (uint16_t *) dark_img->data;

	int x = 0;

	printf("stride = %d, width = %d\n", light_img->stride, width);

	for (i = 0; i < light_img->stride+1228*4; i += 2) {
		int diff, gain;

		diff = *Ln - *Dn;
		if (diff == 0) {
			div_by_zero = 1;
			diff = target;
		}
		gain = (A * target) / diff;
		if (gain > 0xffff) {
			gain_overflow = 1;
			gain = 0xffff;
		}


		if (i < light_img->stride && x % 3 == 0) {
			*p++ = *Dn;
			*p++ = gain;
		} else {
			*p++ = 0;
			*p++ = 0x1000;
		}

		Ln++;
		Dn++;
		x++;
	}

	DBG(DBG_msg, "size = %d bytes\n", *len);

	if (div_by_zero)
		DBG(DBG_warn, "division by zero detected.\n");
	if (gain_overflow)
		DBG(DBG_warn, "gain overflow detected.\n");

	free(light_img);
	free(dark_img);
	return 0;

chk_mem_failed:
	ret = LIBUSB_ERROR_NO_MEM;
chk_failed:
	free(light_img);
	free(dark_img);
	free(*buf);
	*buf = NULL;
	*len = 0;
	return ret;
}

int do_warmup_scan(struct gl843_device *dev, float y_pos)
{
	int ret;

	int start_x = 46;
	int width = 10208;
	int height = 10;
	int dpi = 4800;

	enum gl843_pixformat fmt = PXFMT_RGB16;
	struct gl843_image *img;

	uint16_t *shading = NULL;
	size_t shading_len;

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
			/* start_x */ start_x,
			/* width */ width * 4800 / dpi,
			/* dpi */ dpi,
			/* afe_dpi */ 1200,
			/* linesel */ 0,
		  	/* tgtime */ 0,
			/* lperiod */ 11640,
			/* expr,g,b */ 40000, 40000, 40000));

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

	CHK(set_lamp(dev, LAMP_OFF, 0));
	CHK(find_blacklevel(dev, img, 75, 0));
	CHK(set_lamp(dev, LAMP_PLATEN, 4));

	CHK(warm_up_lamp(dev, img));

	CHK(set_gain(dev, img));

	CHK(calculate_shading(dev, width, height, 0x4000,
		&shading, &shading_len));
	CHK(set_lamp(dev, LAMP_PLATEN, 4));

	uint16_t *dmy = calloc(width*12,1);
	if (dmy)
		CHK(send_shading(dev, dmy, width*12, 0));
	free(dmy);

	if (shading)
		CHK(send_shading(dev, shading, shading_len, 0));
	free(shading);

	int dvdset = 1, shdarea = 1, aveenb = 1;

	struct regset_ent postprocessing[] = {
		/* 0x01 */
		{ GL843_DVDSET, dvdset },
		{ GL843_STAGGER, 0 },
		{ GL843_COMPENB, 0 },
		{ GL843_SHDAREA, shdarea },
		/* 0x03 */
		{ GL843_AVEENB, aveenb }, 	/* X scaling: 1=avg, 0=del */
		/* 0x06 */
		{ GL843_GAIN4, 0 },		/* 0/1: shading gain of 4/8. */
	};
	CHK(write_regs(dev, postprocessing, ARRAY_SIZE(postprocessing)));

	CHK(scan_img(dev, img, 10000));
	write_image("test.pnm", img);
	destroy_image(img);

	CHK(move_carriage(dev, -y_pos));
	while (!read_reg(dev, GL843_HOMESNR))
		usleep(10000);
	CHK(write_reg(dev, GL843_MTRPWR, 0));

	ret = 0;
chk_failed:
	return ret;
}

