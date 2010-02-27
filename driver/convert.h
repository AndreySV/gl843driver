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

#ifndef CONVERT_INTERNAL

#ifndef _CONVERT_H_
#define _CONVERT_H_

struct pixel_converter
{
	uint8_t *buf;	/* Circular pixel buffer */
	int size;	/* Buffer capacity [number of color components] */
	int depth;	/* Number of bits per color component */
	int ncomp;	/* Number of color components per pixel, e.g 1 or 3 */
	int *wr;	/* List of write offsets. Has ncomp elements. */
	int rd;		/* Read offset  */

	/* Pixel converter method. Convert given pixels in-place.
	 * buf:   pixels to convert
	 * count: number of pixels
	 * Returns: number of pixels processed, if any. buf is updated
	 * Note: may return less than 'count' pixels, including none.
	 */
	size_t (*convert)(struct pixel_converter *, uint8_t *buf, size_t count);
};

struct pixel_converter *create_pixel_converter(
	int depth, int ncomp, int *shifts, int dir);
void destroy_pixel_converter(struct pixel_converter *pconv);

#endif /* _CONVERT_H_ */

#else /* CONVERT_INTERNAL */

static size_t CONVERT(struct pixel_converter *pc,
		      uint8_t* pixels,
		      size_t count)
{
	int i, j;
	CTYPE *px = (CTYPE *) pixels;
	CTYPE *buf = (CTYPE *) pc->buf;
	int size = pc->size;
	int ncomp = pc->ncomp;
	int rd = pc->rd;
	int *wr = pc->wr;
	size_t N = 0;

	for (i = 0; i < count; i++) {
		/* Store a pixel, optionally rearranging the components,
		 * and possibly swapping the endianness. */
		for (j = 0; j < ncomp; j++) {
			buf[wr[j]] = BSWAP(*px++);
			wr[j] = (wr[j] + ncomp) % size;
		}

		/* Return a pixel, if any are available. */
		if (rd >= 0) {
			for (j = 0; j < ncomp; j++) {
				px[-ncomp + j] = buf[rd + j];
			}
			rd = (rd + ncomp) % size;
			N++;
		}
	}

	pc->rd = rd;
	return N;
}

#undef CONVERT
#undef CTYPE
#undef BSWAP

#endif /* CONVERT_INTERNAL */
