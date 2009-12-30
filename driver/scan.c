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
#include <string.h>
#include <sane/sane.h>
#include "low.h"
#include "cs4400f.h"
#include "util.h"
#include "scan.h"

struct gl843_image *create_image(int width, int height,
				 enum gl843_pixformat fmt)
{
	int bpp = fmt; /* fmt is enumerated as bits per pixel */
	int stride = ALIGN(bpp * width, 8) / 8;
	struct gl843_image *img;

	img = malloc(sizeof(*img) + stride * height);
	img->bpp = fmt;
	img->width = width;
	img->height = height;
	img->stride = stride;
	img->len = img->stride * img->height;
	return img;
}

void write_pnm_image(const char *filename, struct gl843_image *img)
{
	int do_swap;
	enum gl843_pixformat fmt = img->bpp;

	if (fmt == PXFMT_UNDEFINED) {
		DBG(DBG_error0, "Undefined pixel format\n");
		return;
	}
	FILE *file = fopen(filename, "w");
	if (!file) {
		DBG(DBG_error0, "Cannot open image file %s for writing: %s\n",
			filename, strerror(errno));
		return;
	}

	switch(fmt) {
	case PXFMT_LINEART:
		fprintf(file, "P4\n%d %d\n", img->width, img->height);
		break;
	case PXFMT_GRAY8:
		fprintf(file, "P5\n%d %d\n255\n", img->width, img->height);
		break;
	case PXFMT_GRAY16:
		fprintf(file, "P5\n%d %d\n65535\n", img->width, img->height);
		break;
	case PXFMT_RGB8:
		fprintf(file, "P6\n%d %d\n255\n", img->width, img->height);
		break;
	case PXFMT_RGB16:
		fprintf(file, "P6\n%d %d\n65535\n", img->width, img->height);
		break;
	default:
		break;
	}

	do_swap = (fmt == PXFMT_GRAY16 || fmt == PXFMT_RGB16)
		&& host_is_little_endian();

	if (do_swap) {
		swap_buffer_endianness((uint16_t *)img->data,
			(uint16_t *)img->data, img->len / 2);
	}

	if (fwrite(img->data, img->len, 1, file) != 1) {
		DBG(DBG_error0, "Error writing %s: %s\n",
			filename, strerror(errno));
	}

	if (do_swap) {
		swap_buffer_endianness((uint16_t *)img->data,
			(uint16_t *)img->data, img->len / 2);
	}
}

/* Move the scanner carriage without scanning.
 *
 * This function moves the scanner head to a calibration or
 * lamp warmup position, or back home once completed.
 *
 * d: moving distance in inches. > 0: forward, < 0: back
 *    WARNING: A bad value for d can crash the carriage into the wall.
 */
static int move_carriage(struct gl843_device *dev, float d)
{
	int ret;
	int feedl;
	struct motor_accel move;

	feedl = (int)(4800 * d + 0.5);
	if (feedl >= 0) {
		set_reg(dev, GL843_MTRREV, 0);
	} else {
		set_reg(dev, GL843_MTRREV, 1);
		feedl = -feedl;
	}

	build_accel_profile(&move, 24576, 240, 1.5);

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
		{ GL843_STEPSEL, HALF_STEP },
		{ GL843_STEPNO, 1 },
		{ GL843_FSHDEC, 1 },
		{ GL843_VRSCAN, 3 },	// FIXME: Not hard coded ...
		/* Backtracking (table 2) */
		{ GL843_FASTNO, 1 },
		{ GL843_VRBACK, 3 },	// FIXME: Not hard coded ...
		/* Fast feeding (table 4) and go-home (table 5) */
		{ GL843_FSTPSEL, HALF_STEP },
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

	send_motor_accel(dev, 1, move.a, 1020);
	send_motor_accel(dev, 2, move.a, 1020);
	send_motor_accel(dev, 3, move.a, 1020);
	send_motor_accel(dev, 4, move.a, 1020);
	send_motor_accel(dev, 5, move.a, 1020);

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
 * Calculate and set the AFE offset.
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
static int calc_afe_blacklevel(struct gl843_device *dev,
			       struct calibration_info *cal,
			       uint8_t low, uint8_t high)
{
	int ret, i;
	struct gl843_image *img;
	struct img_stat lo_stat, hi_stat;

	DBG(DBG_msg, "Calibrating A/D-converter black level.\n");

	CHK_MEM(img = create_image(cal->width, cal->height, PXFMT_RGB16));

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
		o = (int) satf(-c / m + 0.5, 0, 255);
		cal->offset[i] = o;
		CHK(write_afe(dev, 32 + i, o));
		DBG(DBG_info, "AFE %s offset = %d\n", idx_name(i), o);
	}

	ret = 0;
chk_failed:
	free(img);
	return ret;	
chk_mem_failed:
	ret = LIBUSB_ERROR_NO_MEM;
	goto chk_failed;
}

/* Get lamp-warmup progress, in percent. */
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
 * Note: Don't forget to turn it on first.
 */
int warm_up_lamp(struct gl843_device *dev,
		 struct calibration_info *cal)
{
	int ret, i;
	int n;			/* Number of scans */
	struct gl843_image *img;
	struct img_stat s;

	float L = 0, L_prev;	/* Lamp intensity */
	float dL = -1, dL_prev;	/* Lamp intensity delta */
	float dL_start = -1;	/* Inital intensity delta */
	float dL_end = 50;	/* Target intensity delta */

	DBG(DBG_msg, "Warming up lamp.\n");

	CHK_MEM(img = create_image(cal->width, cal->height, PXFMT_RGB16));

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

			if (dL_start < dL) {
				dL_start = dL;
				dL_prev = dL;
			}

			p = get_progress(dL_start, dL_end, dL_prev, dL);

			DBG(DBG_info, "  L = %.2f, dL = %.2f\n", L, dL);
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
	free(img);
	return ret;	
chk_mem_failed:
	ret = LIBUSB_ERROR_NO_MEM;
	goto chk_failed;
}

/* Calculate the AFE gain */
static int calc_afe_gain(struct gl843_device *dev,
			 struct calibration_info *cal)
{
	int ret, i;
	float g[3];
	struct gl843_image *img;
	struct img_stat stat;
	/* Target at 95% of max allows lamp brightness increase after warmup. */
	const float target = 65535 * 0.95;
	int gain_overflow = 0;

	DBG(DBG_msg, "Calibrating A/D-converter gain.\n");

	CHK_MEM(img = create_image(cal->width, cal->height, PXFMT_RGB16));

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
		gain_overflow |= (g[i] > max_afe_gain());
		cal->gain[i] = g[i];
		DBG(DBG_info, "%s gain = %.2f, val = %d\n",
			idx_name(i), g[i], afe_gain_to_val(g[i]));
		CHK(write_afe_gain(dev, i, g[i]));
	}

	if (gain_overflow) {
		DBG(DBG_warn, "Gain is too high, (R, G, B) = (%f, %f, %f). "
			"Is the lamp on?\n", g[0], g[1], g[2]);
	}

	ret = 0;
chk_failed:
	free(img);
	return ret;	
chk_mem_failed:
	ret = LIBUSB_ERROR_NO_MEM;
	goto chk_failed;
}

static int calc_shading(struct gl843_device *dev,
			struct calibration_info *cal)
{
	int ret, i;
	struct gl843_image *light_img = NULL, *dark_img = NULL;
	uint16_t *Ln, *Dn, *p, *p_end;
	const int target = 0xffff;
	int div_by_zero = 0;
	int gain_overflow = 0;

	DBG(DBG_msg, "Calculating shading correction.\n");

	CHK_MEM(light_img = create_image(cal->width, cal->height, PXFMT_RGB16));
	CHK_MEM(dark_img = create_image(cal->width, cal->height, PXFMT_RGB16));

	/* Scan light (white) pixels */

	/* Assume lamp is on */
	CHK(scan_img(dev, light_img, 10000));
	get_vertical_average(light_img);

	/* Scan dark (black) pixels */

	CHK(set_lamp(dev, LAMP_OFF, 0));
	CHK(scan_img(dev, dark_img, 10000));
	get_vertical_average(dark_img);

	/* Calculate shading
	 * Ref: shading & correction in GL843 datasheet. */

	p = cal->sc;
	p_end = p + cal->sc_len;
	Ln = (uint16_t *) light_img->data;
	Dn = (uint16_t *) dark_img->data;

	for (i = 0; i < cal->width * 3; i++) {
		int diff, gain;

		diff = *Ln++ - *Dn;
		if (diff == 0) {
			div_by_zero = 1;
			diff = target;
		}
		gain = (cal->A * target) / diff;
		if (gain > 0xffff) {
			gain_overflow = 1;
			gain = 0xffff;
		}

		*p++ = *Dn++;
		*p++ = gain;

		if (p > p_end) {
			DBG(DBG_error, "internal error: buffer overrun.");
			break;
		}
	}

	if (host_is_big_endian())
		swap_buffer_endianness(cal->sc, cal->sc, cal->sc_len / 2);

	if (div_by_zero)
		DBG(DBG_warn, "division by zero detected.\n");
	if (gain_overflow)
		DBG(DBG_warn, "gain overflow detected.\n");

	ret = 0;
chk_failed:
	free(light_img);
	free(dark_img);
	return ret;
chk_mem_failed:
	ret = LIBUSB_ERROR_NO_MEM;
	goto chk_failed;
}

struct calibration_info *create_calinfo(enum gl843_lamp source,
					float cal_y_pos,
					int start_x,
					int width,
					int height,
					int dpi)
{
	struct calibration_info *cal;
	int sc_len = width * 12;

	CHK_MEM(cal = calloc(sizeof(*cal) + sc_len, 1));
	cal->source = source;
	cal->cal_y_pos = cal_y_pos;
	cal->width = width;
	cal->start_x = start_x;
	cal->height = height;
	cal->dpi = dpi;
	cal->sc_len = sc_len;
	cal->A = 0x2000;

chk_mem_failed:
	return cal;
}

int do_warmup_scan(struct gl843_device *dev, float cal_y_pos)
{
	int ret;

	enum gl843_lamp lamp = LAMP_PLATEN;
	int lamp_to = 4;
	int start_x = 128;
	int width = 10208;
	int height = 16;
	int dpi = 4800;

	struct calibration_info *cal;

	CHK_MEM(cal = create_calinfo(lamp, cal_y_pos, start_x, width, height, dpi));

	CHK(write_reg(dev, GL843_SCANRESET, 1));
	while(!read_reg(dev, GL843_HOMESNR))
		usleep(10000);
	CHK(do_base_configuration(dev));

	CHK(move_carriage(dev, cal_y_pos));
	while (read_reg(dev, GL843_MOTORENB))
		usleep(10000);

	CHK(setup_ccd_and_afe(dev,
			/* fmt */ PXFMT_RGB16,
			/* start_x */ start_x,
			/* width */ width * 4800 / dpi,
			/* dpi */ dpi,
			/* afe_dpi */ 1200,
			/* linesel */ 0,
		  	/* tgtime */ 0,
			/* lperiod */ 11640,
			/* expr,g,b */ 40000, 40000, 40000));

	CHK(select_shading(dev, SHADING_CORR_OFF));

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
	CHK(calc_afe_blacklevel(dev, cal, 75, 0));
	CHK(set_lamp(dev, lamp, lamp_to));
	CHK(warm_up_lamp(dev, cal));
	CHK(calc_afe_gain(dev, cal));
	CHK(calc_shading(dev, cal));
	CHK(set_lamp(dev, lamp, lamp_to));
	CHK(send_shading(dev, cal->sc, cal->sc_len, 0));

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

	CHK(move_carriage(dev, -cal_y_pos));
	while (!read_reg(dev, GL843_HOMESNR))
		usleep(10000);
	CHK(write_reg(dev, GL843_MTRPWR, 0));

	free(cal);

	ret = 0;
chk_failed:
	return ret;
chk_mem_failed:
	ret = LIBUSB_ERROR_NO_MEM;
	goto chk_failed;	
}

static int do_move(struct gl843_device *dev)
{
	int ret = 0;
	int moving = 1;
	write_reg(dev, GL843_MOVE, 255);
	while (moving) {
		CHK(read_regs(dev, GL843_MOTORENB, GL843_FEDCNT, -1));
		moving = get_reg(dev, GL843_MOTORENB);
		printf("\rhomesnr = %d, fedcnt = %d        ",
			get_reg(dev, GL843_HOMESNR),
			get_reg(dev, GL843_FEDCNT));
		usleep(1000);
	}
	printf("\n");
chk_failed:
	return ret;
}

/* Use this function to explore the scanner motor settings. */
int do_move_test(struct gl843_device *dev,
		 int distance,
		 int start_speed,
		 int end_speed,
		 float exp,
		 int vref)
{
	int ret = 0;
	struct dbg_timer tmr;
	struct motor_accel m;
	enum motor_steptype step = HALF_STEP;

	init_timer(&tmr, CLOCK_REALTIME);

	build_accel_profile(&m, start_speed, end_speed, exp);
	CHK(send_motor_accel(dev, 1, m.a, 1020));
	CHK(write_reg(dev, GL843_CLRMCNT, 1));	/* Clear FEDCNT */

	/* Move forward */

	set_reg(dev, GL843_STEPNO, m.alen >> STEPTIM);
	set_reg(dev, GL843_STEPTIM, STEPTIM);
	set_reg(dev, GL843_VRMOVE, vref);

	set_reg(dev, GL843_FEEDL, distance);
	set_reg(dev, GL843_STEPSEL, step);

	set_reg(dev, GL843_MTRREV, 0);
	set_reg(dev, GL843_MTRPWR, 1);
	CHK(flush_regs(dev));

	CHK(do_move(dev));

	usleep(100000);

	/* Back up again */

	set_reg(dev, GL843_FEEDL, distance);
	set_reg(dev, GL843_MTRREV, 1);
	CHK(flush_regs(dev));
	CHK(do_move(dev));

	set_reg(dev, GL843_MTRPWR, 0);
	set_reg(dev, GL843_FULLSTP, 1);
	CHK(flush_regs(dev));

	printf("elapsed time: %f [ms]\n", get_timer(&tmr));

	usleep(100000);

	/* Reset, in case the motor stalled */

	set_reg(dev, GL843_SCANRESET, 0);
	flush_regs(dev);
chk_failed:
	return ret;
}

