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
#include <string.h>
#include "low.h"
#include "util.h"
#include "image.h"

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

uint16_t *get_shading(uint16_t *darkscan,
		      uint16_t *lightscan,
		      uint16_t target,
		      unsigned int G,
		      size_t n)
{
	int i;
	uint16_t *shading;

	shading = malloc(n * 12);
	if (!shading)
		return NULL;

	if (target < 1)
		target = 1;

	if (G == 4)
		G = 0x4000;
	else /* if (G == 8) */
		G = 0x2000;

	for (i = 0; i < 3*n; i++) {
		int diff = *lightscan++ - *darkscan++;
		diff = (diff <= 0) ? target : diff; /* Avoid div by zero */
		*shading++ = G * target / diff;
	}
	return shading;
}

