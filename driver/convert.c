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
 * ncomp:  Number of pixel components, typically 3 for RGB.
 * shifts: List of pixel component offsets. List length is given in ncomp.
 *         Incoming data will be buffered at an offset, given in shifts[i]
 *         where 0 <= i <= ncomp-1 is the pixel component number.
 *         The unit of the offset values are "number of components".
 *         See below for examples.
 * scanner_endianness:    1 = little endian, 2 = big endian
 *
 * Note:
 *
 * Some examples of shifts. ncomp = 3.
 *
 * 1. No operation (RGB->RGB, no pixel shifting):       shifts[] = { 0,  1, 2}
 * 2. Convert BGR to RGB:                               shifts[] = { 2,  1, 0}
 * 3. Shift red 5 pixels, green 10, blue zero pixels:   shifts[] = {15, 31, 2}
 * 4. Combination of 2 and 3 (BGR->RGB and shift):      shifts[] = {17, 31, 0}
 */
struct pixel_converter *create_pixel_converter(int depth,
					       int ncomp,
					       int *shifts,
					       int scanner_endianness)
{
	int i;
	int s_min, s_max; /* Min/max shift */
	struct pixel_converter *pconv;

	if (ncomp > 1) {
		s_min = shifts[0];
		s_max = shifts[0];
		for (i = 1; i < ncomp; i++) {
			if (shifts[i] > s_max)
				s_max = shifts[i];
			if (shifts[i] < s_min)
				s_min = shifts[i];
		}
	} else {
		s_min = 0;
		s_max = ncomp;
	}
	CHK_MEM(pconv = calloc(sizeof(*pconv), 1));

	if (depth == 8) {
		pconv->convert = convert8;
	} else if (depth == 16) {
		if (native_endianness() != scanner_endianness) {
			pconv->convert = convert16_swap;
		} else {
			pconv->convert = convert16;
		}
	} else {
		DBG(DBG_error0, "BUG: unsupported pixel depth\n");
		goto chk_mem_failed;
	}


	CHK_MEM(pconv->wr = calloc(sizeof(*(pconv->wr)) * ncomp, 1));

	pconv->ncomp = ncomp;
	pconv->depth = depth;
	pconv->size = s_max - s_min;

	if (pconv->size % ncomp != 0) {
		/* Round up buffer size to hold full pixels */
		pconv->size += ncomp - (pconv->size % ncomp);
	}

	pconv->rd = -pconv->size; 	 /* rd < 0 delays reads. */

	CHK_MEM(pconv->buf = calloc(pconv->size * depth / 8, 1));
	
	for (i = 0; i < ncomp; i++) {
		pconv->wr[i] = shifts[i] - s_min;
	}

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

