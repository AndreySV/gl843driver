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
		fprintf(file, "P6\n%d %d\n255\n", img->width-8, img->height);
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

/* read_image_data - Read available image data from the scanner. */
int read_image_data(struct gl843_device *dev,
		    struct gl843_image *img,
		    int linecnt)
{
	int ret, n, m;
	n = linecnt * img->stride;

	img->data = realloc(img->data, img->len + n);
	ret = recv_image(dev, img->data + img->len, n, 0);
	if (ret < 0)
		return ret;
	img->len += n;
	img->height += linecnt;

	return 0;
}

struct gl843_image img;
int init_afe(struct gl843_device *dev);
void set_postprocessing(struct gl843_device *dev);
void mark_devreg_dirty(struct gl843_device *dev, enum gl843_reg reg);

void sigint_handler(int sig)
{
	write_image("test.pnm", &img);
	exit(0);
}

int main()
{
	libusb_context *ctx;
	struct gl843_device dev;

	enum gl843_pixformat fmt = PXFMT_RGB16;

	int ret, moving;

	signal(SIGINT, sigint_handler);

	init_debug("GL843", -1);
	ctx = open_scanner(&dev, 0x04a9, 0x2228);
	libusb_set_debug(ctx, 3);
	if (ctx == NULL)
		return 1;

	write_reg(&dev, GL843_SCANRESET, 1);
	write_reg(&dev, GL843_SCANRESET, 0);

	usleep(100000);

	while(!read_reg(&dev, GL843_HOMESNR))
		usleep(10000);

	setup_scanner(&dev);
	init_afe(&dev);
	send_simple_gamma(&dev, 1.0);

	int width = 2552;
	int height = 2400;
	int dpi = 1200;

	create_image(&img, fmt, width);
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

	setup_scanning_profile(&dev,
			   0.5 /* y_start */,
			   //1.0625 /* y_end */,
			   height,
			   600 /* y_dpi */,
			   HALF_STEP /* type */,
			   200 /* fwdstep 0 = disable */,
			   11640 /* exposure */);

	set_reg(&dev, GL843_CLRMCNT, 0);
	set_reg(&dev, GL843_CLRLNCNT, 0);
	set_reg(&dev, GL843_MTRREV, 0);
	set_reg(&dev, GL843_NOTHOME, 0);
	set_reg(&dev, GL843_AGOHOME, 1);
	set_reg(&dev, GL843_MTRPWR, 1);
	set_reg(&dev, GL843_OPTEST,0);
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

	//TODO: Read data quickly and reliably

	

	do {
		int vword, linecnt;

		vword = read_reg(&dev, GL843_VALIDWORD);
		linecnt = (vword & ~255) * 2 / img.width;

		if (linecnt > 0) {
			read_image_data(&dev, &img, linecnt);
		} else {
			usleep(1000);
		}
	
	} while(!read_reg(&dev, GL843_SCANFSH));

//	while (!read_reg(&dev, GL843_FEEDFSH));
//	while (!read_reg(&dev, GL843_BUFEMPTY));
//	ret = read_image_data(&dev, &img, height);

	write_image("test.pnm", &img);

	destroy_image(&img);

	while(!read_reg(&dev, GL843_HOMESNR))
		usleep(10000);
	write_reg(&dev, GL843_MTRPWR, 0);
	libusb_close(dev.libusb_handle);
	return 0;

//usb_fail:
	printf("usb error: %s\n", sanei_libusb_strerror(ret));
	libusb_close(dev.libusb_handle);
	return 0;
}
