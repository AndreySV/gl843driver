/* Test application for canoscan 4400F (gl843) driver development
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
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <math.h>

#include "gl843_low.h"
#include "gl843_util.h"
#include "gl843_cs4400f.h"
#include "gl843_priv.h"
#include "gl843_motor.h"

struct gl843_image
{
	int bpp;		/* Bits per pixel 1, 8, 16, 24 or 48 */
	int width;		/* Pixels per line */
	int stride;		/* Bytes per line */
	int height;		/* Number of lines */
	size_t len;		/* Data buffer length, in bytes */
	uint8_t data[0];	/* Data buffer follows */
};

void send_simple_gamma(struct gl843_device *dev, float gamma)
{
	const int N = 256;
	uint16_t g[N];
	int k;

	for (k = 0; k < N; k++) {
		g[k] = (uint16_t) (65535 * powf((float)k / N, 1/gamma) + 0.5);
		//g[k] = 0;//(256-k) * 256;
	}
	send_gamma_table(dev, 1, N, g);
	send_gamma_table(dev, 2, N, g);
	send_gamma_table(dev, 3, N, g);
}

struct gl843_image *create_image(int width,
				 int height,
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

void destroy_image(struct gl843_image *img)
{
	free(img);
}

void write_image(const char *filename, struct gl843_image *img)
{
	enum gl843_pixformat fmt = img->bpp;

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
	}

	if ((fmt == PXFMT_GRAY16 || fmt == PXFMT_RGB16)
		&& host_is_little_endian())
	{
		swap_buffer_endianness((uint16_t *)img->data,
			(uint16_t *)img->data, img->len / 2);
	}

	if (fwrite(img->data, img->len, 1, file) != 1) {
		DBG(DBG_error0, "Error writing %s: %s\n",
			filename, strerror(errno));
	}
}

static libusb_context *open_scanner(struct gl843_device *dev, int pid, int vid)
{
	int ret;
	int have_iface = 0;
	libusb_context *ctx = NULL;
	libusb_device_handle *h;

	ret = libusb_init(&ctx);
	if (ret < 0) {
		fprintf(stderr, "Can't initialize libusb.\n");
		return NULL;
	}
	h = libusb_open_device_with_vid_pid(NULL, pid, vid);
	if (!h) {
		fprintf(stderr, "Can't find the scanner.\n");
		return NULL;
	}

	ret = libusb_set_configuration(h, 1);
	if (ret < 0)
		goto usb_error;
	ret = libusb_claim_interface(h, 0);
	if (ret < 0)
		goto usb_error;

	create_device(dev);
	dev->libusb_handle = h;
	return ctx;

usb_error:
	if (have_iface) {
		libusb_release_interface(h, 0);
	}
	if (ret < 0) {
		fprintf(stderr, "USB error. return code %d.\n", ret);
	}
	libusb_close(h);
	return NULL;
}

struct gl843_image *img = NULL;
int init_afe(struct gl843_device *dev);
void set_postprocessing(struct gl843_device *dev);
void mark_devreg_dirty(struct gl843_device *dev, enum gl843_reg reg);

void sigint_handler(int sig)
{
	if (img)
		write_image("test.pnm", img);
	exit(0);
}

/* Move the scanner head without scanning.
 *
 * This function is intended to move the head to its calibration or
 * lamp warmup positions, and back home once completed.
 *
 * y_pos: > 0: forward distance in inches. 0: go home. < 0: Invalid
 */
int move_scanner_head(struct gl843_device *dev, float y_pos)
{
	struct gl843_motor_setting move;

	float Km;	/* Number of steps per inch */
	int feedl;

	cs4400f_get_fast_feed_motor_table(&move);

	Km = dev->base_ydpi << move.type;

	if (y_pos > 0) {
		/* Move forward */
		set_reg(dev, GL843_MTRREV, 0);

		feedl = ((int) (Km * y_pos + 0.5)) - 2 * move.alen;
		if (feedl < 0) {
			/* The acceleration/deceleration curves are longer than
			 * the distance we wish to move. Set feedl = 0 and trim
			 * the curves down the right lengths. */
			move.alen = (feedl + 2 * move.alen) / 2;
			feedl = 0;
		}
		dev->head_pos += y_pos; /* Assume the move is successful */

	} else if (y_pos == 0) {
		/* Move home */

		if (dev->head_pos == 0)
			return 0;

		set_reg(dev, GL843_MTRREV, 1);

		/* If the head is away from home, and the distance back is
		 * shorter than the acceleration curve, the head will hit
		 * the wall. If so, shorten the accel-curve first. */
		feedl = ((int) (Km * dev->head_pos + 0.5)) - 2 * move.alen;
		if (feedl < 0) {
			move.alen = (feedl + 2 * move.alen) / 2;
			feedl = 0;
		}
		dev->head_pos = 0; /* Assume the move is successful */
		
	} else { /* if (y_pos < 0) */
		return -EINVAL;
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
		{ GL843_SCANFED, 1 },
		{ GL843_FEEDL, feedl },
		{ GL843_LINCNT, 0 },
		{ GL843_ACDCDIS, 1 }, /* Disable backtracking. */

		{ GL843_Z1MOD, 0 },
		{ GL843_Z2MOD, 0 },
	};
	set_regs(dev, motor1, ARRAY_SIZE(motor1));
	if (flush_regs(dev) < 0)
		return -EIO;

	send_motor_table(dev, 1, 1020, move.a);
	send_motor_table(dev, 2, 1020, move.a);
	send_motor_table(dev, 3, 1020, move.a);
	send_motor_table(dev, 4, 1020, move.a);
	send_motor_table(dev, 5, 1020, move.a);

	set_reg(dev, GL843_CLRMCNT, 1);		/* Clear FEDCNT */
	set_reg(dev, GL843_CLRLNCNT, 1);	/* Clear SCANCNT */
	set_reg(dev, GL843_NOTHOME, 0);
	set_reg(dev, GL843_AGOHOME, 1);
	flush_regs(dev);

	write_reg(dev, GL843_MTRPWR, 1);
	write_reg(dev, GL843_SCAN, 0);
	write_reg(dev, GL843_MOVE, 16);

	return 0;
}

void calibrate_adc(struct gl843_device *dev)
{
	setup_scanner(dev);
	init_afe(dev);
	send_simple_gamma(dev, 1.0);

	int width = 2552;
	int height = 101; //107;
	int dpi = 1200;
	int y_dpi = 300;
	enum gl843_pixformat fmt = PXFMT_RGB16;

	move_scanner_head(dev, 0.3);
	while (read_reg(dev, GL843_MOTORENB));

	img = create_image(width, height, fmt);
	set_frontend(dev,
			/* fmt */ fmt,
			/* width */ width * 4800 / dpi,
			/* start_x */ 128,
			/* dpi */ dpi,
			/* afe_dpi */ dpi,
			/* linesel */ 0,
		  	/* tgtime */ 0,
			/* lperiod */ 11640,
			/* expr,g,b */ 40000, 40000, 40000);

	//set_lamp(dev, LAMP_OFF, 0);
	set_lamp(dev, LAMP_PLATEN, 1);		/* Turn on lamp */
	set_postprocessing(dev);
	flush_regs(dev);

	setup_scanning_profile(dev,
			   0.25 /* y_start */,
			   //1.0625 /* y_end */,
			   height,
			   300 /* y_dpi */,
			   HALF_STEP /* type */,
			   0 /* fwdstep 0 = disable */,
			   11640 /* exposure */);

	set_reg(dev, GL843_MTRREV, 0);
	set_reg(dev, GL843_NOTHOME, 0);
	flush_regs(dev);

	set_reg(dev, GL843_CLRLNCNT, 1);	/* Clear SCANCNT */
	flush_regs(dev);

	write_reg(dev, GL843_MTRPWR, 0);
	write_reg(dev, GL843_AGOHOME, 0);
	write_reg(dev, GL843_SCAN, 1);
	write_reg(dev, GL843_MOVE, 255);

	while (read_reg(dev, GL843_BUFEMPTY));
	recv_image(dev, img->data, img->stride, img->height);
	write_image("test.pnm", img);
	destroy_image(img);

	move_scanner_head(dev, 0);
	while (!read_reg(dev, GL843_HOMESNR));
	write_reg(dev, GL843_MTRPWR, 0);


}

int main()
{
	libusb_context *ctx;
	struct gl843_device dev;

	int ret;

	signal(SIGINT, sigint_handler);

	init_debug("GL843", -1);
	ctx = open_scanner(&dev, 0x04a9, 0x2228);
	libusb_set_debug(ctx, 3);
	if (ctx == NULL)
		return 1;

	write_reg(&dev, GL843_SCANRESET, 1);
	write_reg(&dev, GL843_SCANRESET, 0);

	while(!read_reg(&dev, GL843_HOMESNR))
		usleep(10000);

	setup_scanner(&dev);
	init_afe(&dev);
	send_simple_gamma(&dev, 1.0);

	int width = 2552;
	int height = 100;
	int dpi = 1200;
	enum gl843_pixformat fmt = PXFMT_RGB8;

	img = create_image(width, height, fmt);
	set_frontend(&dev,
			/* fmt */ fmt,
			/* width */ width * 4800 / dpi,
			/* start_x */ 128,
			/* dpi */ dpi,
			/* afe_dpi */ dpi,
			/* linesel */ 0,
		  	/* tgtime */ 0,
			/* lperiod */ 11640,
			/* expr,g,b */ 40000, 40000, 40000);

	//set_lamp(&dev, LAMP_OFF, 0);
	set_lamp(&dev, LAMP_PLATEN, 1);		/* Turn on lamp */
	set_postprocessing(&dev);
	flush_regs(&dev);

	calibrate_adc(&dev);
#if 0
	setup_scanning_profile(&dev,
			   0.2 /* y_start */,
			   //1.0625 /* y_end */,
			   height,
			   300 /* y_dpi */,
			   HALF_STEP /* type */,
			   200 /* fwdstep 0 = disable */,
			   11640 /* exposure */);

	set_reg(&dev, GL843_CLRMCNT, 0);
	set_reg(&dev, GL843_CLRLNCNT, 0);
	set_reg(&dev, GL843_MTRREV, 0);
	set_reg(&dev, GL843_NOTHOME, 0);
	set_reg(&dev, GL843_AGOHOME, 1);
	set_reg(&dev, GL843_MTRPWR, 1);
	flush_regs(&dev);

	set_reg(&dev, GL843_CLRMCNT, 1);	/* Clear FEDCNT */
	set_reg(&dev, GL843_CLRLNCNT, 1);	/* Clear SCANCNT */
	flush_regs(&dev);
	set_reg(&dev, GL843_CLRMCNT, 0);
	set_reg(&dev, GL843_CLRLNCNT, 0);
	flush_regs(&dev);

	//write_reg(&dev, GL843_BUFSEL, 16); /* Is ignored by the scanner. (?)*/
	write_reg(&dev, GL843_SCAN, 1);
	write_reg(&dev, GL843_MOVE, 255);

	while (read_reg(&dev, GL843_BUFEMPTY));
	recv_image(&dev, img.data, img.len);
	write_image("test.pnm", &img);
	destroy_image(&img);

	while(!read_reg(&dev, GL843_HOMESNR))
		usleep(10000);
#endif
	write_reg(&dev, GL843_MTRPWR, 0);
	libusb_close(dev.libusb_handle);
	return 0;

//usb_fail:
	printf("usb error: %s\n", sanei_libusb_strerror(ret));
	libusb_close(dev.libusb_handle);
	return 0;
}
