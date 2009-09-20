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

#include "gl843_low.h"
#include "gl843_util.h"
#include "gl843_cs4400f.h"
#include "gl843_priv.h"
#include "gl843_motor.h"

struct gl843_image
{
	int bpp;
	int width;
	int stride;
	int height;
	uint8_t *data;
	size_t len;
};

void create_image(struct gl843_image *img, enum gl843_pixformat fmt, int width)
{
	img->bpp = fmt;	/* fmt is enumerated as bits per pixel */
	img->width = width;
	img->stride = ALIGN(img->bpp * img->width, 8) / 8;
	img->height = 0;
	img->data = NULL;
	img->len = 0;
}

void destroy_image(struct gl843_image *img)
{
	free(img->data);
	img->data = NULL;
	img->len = 0;
}

/* read_image_data - Read available image data from the scanner. */
int read_image_data(struct gl843_device *dev,
		    struct gl843_image *img,
		    int linecnt)
{
	int ret, n;
	n = linecnt * img->stride;

	img->data = realloc(img->data, img->len + n);
	memset(img->data + img->len, 0, n);
	ret = xfer_bulk(dev, img->data + img->len, n, 0, BULK_IN | IMG_DRAM);
	if (ret < 0)
		return ret;
	img->len += n;
	img->height += linecnt;

	return 0;
}

void write_image(const char *fname, struct gl843_image *img)
{
	enum gl843_pixformat fmt = img->bpp;

	FILE *file = fopen(fname, "w");
	if (!file) {
		DBG(DBG_error0, "Cannot open image file %s for writing: %s\n",
			fname, strerror(errno));
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
		int i;
		for (i = 0; i < img->len; i += 2) {
			uint8_t v = img->data[i];
			img->data[i] = img->data[i+1];
			img->data[i+1] = v;
		}
	}

	if (fwrite(img->data, img->len, 1, file) != 1) {
		DBG(DBG_error0, "Error writing %s file: %s\n",
			fname, strerror(errno));
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

int init_afe(struct gl843_device *dev);
void set_postprocessing(struct gl843_device *dev);

int main()
{
	libusb_context *ctx;
	struct gl843_device dev;
	struct gl843_image img;

	enum gl843_pixformat fmt = PXFMT_RGB16;

	int ret, moving;

	init_debug("GL843", -1);
	ctx = open_scanner(&dev, 0x04a9, 0x2228);
	libusb_set_debug(ctx, 3);
	if (ctx == NULL)
		return 1;

	create_image(&img, fmt, 2552);

	setup_scanner(&dev);

	set_reg(&dev, GL843_FULLSTP, 0);
	set_reg(&dev, GL843_SCANRESET, 1);
	flush_regs(&dev);

	init_afe(&dev);

	set_frontend(&dev,
			/* fmt */ fmt,
			/* width */ 10208,
			/* start_x */ 128,
			/* dpi */ 1200,
			/* afe_dpi */ 1200,
			/* linesel */ 0,
		  	/* tgtime */ 0,
			/* lperiod */ 11640,
			/* expr,g,b */ 40000, 40000, 40000);

	//signal(SIGINT, sigint_handler);

	moving = 1;
	while (moving) {
		moving = read_regs(&dev, GL843_MOTORENB, -1);
		if (moving == -EIO)
			break;
		usleep(10000);
	}
	set_reg(&dev, GL843_CLRMCNT, 1);	/* Clear FEDCNT */
	set_reg(&dev, GL843_CLRLNCNT, 1);	/* Clear SCANCNT */
	flush_regs(&dev);
	set_reg(&dev, GL843_CLRMCNT, 0);
	set_reg(&dev, GL843_CLRLNCNT, 0);
	flush_regs(&dev);
	usleep(500000);

	set_lamp(&dev, LAMP_PLATEN, 1);		/* Turn on lamp */
	set_postprocessing(&dev);
	flush_regs(&dev);

	setup_scanning_profile(&dev,
			   0.5 /* y_start */,
			   //1.0625 /* y_end */,
			   0.505,
			   600 /* y_dpi */,
			   HALF_STEP /* type */,
			   400 /* fwdstep */,
			   11640 /* exposure */);

	set_reg(&dev, GL843_CLRMCNT, 0);
	set_reg(&dev, GL843_CLRLNCNT, 0);
	set_reg(&dev, GL843_MTRREV, 0);
	set_reg(&dev, GL843_NOTHOME, 0);
	set_reg(&dev, GL843_AGOHOME, 1);
	//set_reg(&dev, GL843_MTRPWR, 1);
	flush_regs(&dev);
	set_reg(&dev, GL843_SCAN, 1);
	flush_regs(&dev);
	set_reg(&dev, GL843_MOVE, 255);
	flush_regs(&dev);

	int done;
	int vword;
	int n;

	while (!read_reg(&dev, GL843_SCANFSH)) {
#if 0
		if (read_reg(&dev, GL843_VALIDWORD)) {
			ret = read_image_data(&dev, &img, 1, 1);
			if (ret < 0)
				return 1;
		}
#endif
		usleep(10000);
	}
	printf("scancnt = %d\n", read_reg(&dev, GL843_SCANCNT));
	printf("validword = %d\n", read_reg(&dev, GL843_VALIDWORD));

	n = 0;
	while (!read_reg(&dev, GL843_BUFEMPTY)) {
		ret = read_image_data(&dev, &img, 1);
		if (ret < 0)
			return 1;
		n++;
	}

	printf("n = %d\n", n);
	if (n > 0)
		write_image("test.pnm", &img);

	while(!read_reg(&dev, GL843_HOMESNR))
		usleep(10000);

	set_reg(&dev, GL843_MTRPWR, 0);
	flush_regs(&dev);

	destroy_image(&img);
	set_lamp(&dev, LAMP_OFF, 0);		/* Turn off lamp */
	flush_regs(&dev);
	libusb_close(dev.libusb_handle);
	return 0;

//usb_fail:
	printf("usb error: %s\n", sanei_libusb_strerror(ret));
	libusb_close(dev.libusb_handle);
	return 0;
}
