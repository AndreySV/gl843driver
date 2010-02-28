/* Generic line processing.
 *
 * Copyright (C) 2010 Andreas Robinson <andr345 at gmail dot com>
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

#define _BSD_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sane/sane.h>

#include "util.h"
#include "convert.h"

#define CONVERT_INTERNAL

static uint16_t my_bswap_16(uint16_t x) {
	return (x >> 8) | (x << 8);
}

/* define convert8() */
#define CONVERT convert8
#define CTYPE uint8_t
#define BSWAP(x) (x)
#include "convert.h"

/* define convert16() */
#define CONVERT convert16
#define CTYPE uint16_t
#define BSWAP(x) (x)
#include "convert.h"

/* define convert16_swap() */
#define CONVERT convert16_swap
#define CTYPE uint16_t
#define BSWAP(x) my_bswap_16(x)
#include "convert.h"

/* Convert between host and scanner endianness,
 * reorder pixel color components (e.g BGR to RGB),
 * and correct for the the RGB line-distances in the scanner CCD.
 *
 * depth:  Number of bits per pixel component (a.k.a channel), 8 or 16.
 * ncomp:  Number of components per pixel, e.g 3 for RGB.
 * shift:  List of pixel component shifts. List length is given by ncomp.
 *         Incoming pixel component i will be delayed for shift[i] pixels.
 *         The unit of the offset values are "number of components".
 * order:  List of pixel component ordering.
 *         {2,1,0} will reorder BGR to RGB (or vice versa), {0,1,2} does nothing.
 * se:     scanner endianness: 1 = little endian, 2 = big endian
 */
struct pixel_converter *create_pixel_converter(int depth,
					       int ncomp,
					       int *shift,
					       int *order,
					       int se)
{
	int i;
	int numpixels;	/* Number of pixels in buffer */
	struct pixel_converter *pconv;

	CHK_MEM(pconv = calloc(sizeof(*pconv), 1));
	CHK_MEM(pconv->wr = calloc(sizeof(*(pconv->wr)) * ncomp, 1));

	if (depth == 8) {
		pconv->convert = convert8;
	} else if (depth == 16) {
		pconv->convert = (native_endianness() != se)
			? convert16_swap : convert16;
	} else {
		DBG(DBG_error0, "BUG: unsupported pixel depth\n");
		goto chk_mem_failed;
	}

	numpixels = 1;
	pconv->wr[0] = 0; /* Ignore shift[] and order[] when ncomp == 1 */
	if (ncomp > 1) {
		for (i = 0; i < ncomp; i++) {
			if (shift[i] > numpixels)
				numpixels = shift[i];
		}
		numpixels++;
		for (i = 0; i < ncomp; i++) {
			pconv->wr[i] = (ncomp * shift[i] + order[i]) % (numpixels * ncomp);
		}
	}
	DBG(DBG_msg, "numpixels = %d, ncomp = %d, depth = %d\n",
		numpixels, ncomp, depth);
	for (i = 0; i < ncomp; i++) {
		DBG(DBG_msg, "wr[%d] = %d\n", i, pconv->wr[i]);
	}

	pconv->sdelay = -numpixels + 1;
	pconv->rd = (numpixels - 1) * ncomp;

	pconv->ncomp = ncomp;
	pconv->depth = depth;
	pconv->numpixels = numpixels;

	CHK_MEM(pconv->buf = calloc(numpixels * ncomp * depth / 8, 1));

	return pconv;

chk_mem_failed:
	destroy_pixel_converter(pconv);
	return NULL;
}	

void destroy_pixel_converter(struct pixel_converter *pconv)
{
	if (pconv) {
		free(pconv->wr);
		free(pconv->buf);
	}
	free (pconv);
}

#if 0

/* Converter unit test */

#include <stdio.h>

void dump_buf(void *p, int n)
{
	int i, j = 0;
	uint16_t *buf = p;
	for (i = 0; i < n; i += 3) {
		printf("%04x %04x %04x    ", buf[i], buf[i+1], buf[i+2]);
		j++;
		if (j == 5) {
			j = 0;
			printf("\n");
		}
	}
	printf("\n");
}

int main()
{
	const int N = 85;
	int i,j,m;
	uint16_t buf[N*3];
	int shift[3] = {20, 10, 0};
	int order[3] = {0, 1, 2};
	struct pixel_converter *pconv;

	memset(buf, 0xff, N*3);

	for (i = 0, j = 0; i < N*3; i += 3) {
		if (i % 15 == 0)
			j += 0x10;
		buf[i] = j+1;
	}
	for (i = 30, j = 0; i < N*3; i += 3) {
		if (i % 15 == 0)
			j += 0x10;
		buf[i+1] = j+2;
	}
	for (i = 60, j = 0; i < N*3; i += 3) {
		if (i % 15 == 0)
			j += 0x10;
		buf[i+2] = j+3;
	}

	dump_buf(buf, N*3);
	pconv = create_pixel_converter(16, 3, shift, order, 1);
	m = pconv->convert(pconv, (uint8_t *)buf, N);
	dump_buf(buf, m*3);
	return 0;
}
#endif

