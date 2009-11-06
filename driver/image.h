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

#ifndef _IMAGE_H_
#define _IMAGE_H_

enum gl843_pixformat {	/* Note; Format enumerations == bits per pixel */
	PXFMT_LINEART = 1,	/* 1 bit per pixel, black and white */
	PXFMT_GRAY8 = 8,	/* 8 bits per pixel, grayscale */
	PXFMT_GRAY16 = 16,	/* 16 bits per pixel, grayscale */
	PXFMT_RGB8 = 24,	/* 24 bits per pixel, RGB color */
	PXFMT_RGB16 = 48,	/* 48 bits per pixel, RGB color */
};

struct gl843_image
{
	int bpp;		/* Bits per pixel 1, 8, 16, 24 or 48 */
	int width;		/* Pixels per line */
	int stride;		/* Bytes per line */
	int height;		/* Number of lines */
	size_t len;		/* Data buffer length, in bytes */
	uint8_t data[0];	/* Data buffer follows */
};

/* Constructor */
struct gl843_image *create_image(int width, int height, enum gl843_pixformat fmt);
/* Destructor */
void destroy_image(struct gl843_image *img);
/* Write image to disk in PNM format */
void write_image(const char *filename, struct gl843_image *img);
/* TODO: Formatting and DSP operations, e.g BGR->RGB reordering. */

/* Calculate gamma correction table
 * gamma: gamma coefficient
 */
void send_simple_gamma(struct gl843_device *dev, float gamma);

#endif /* _IMAGE_H_ */
