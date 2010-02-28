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
	int numpixels;	/* Buffer capacity [number pixels] */
	int depth;	/* Number of bits per color component */
	int ncomp;	/* Number of color components per pixel, */
	int *wr;	/* List of write offsets. Has ncomp elements. */
	int rd;		/* Read offset  */
	int sdelay;	/* Number of pixels to wait before returning data */

	/* Pixel converter method. Convert given pixels in-place.
	 * buf:   pixels to convert
	 * count: number of pixels
	 * Returns: number of pixels returned, if any. buf is updated
	 * Note: may return less than 'count' pixels, including none.
	 */
	size_t (*convert)(struct pixel_converter *, uint8_t *buf, size_t count);
};

struct pixel_converter *create_pixel_converter(
	int depth, int ncomp, int *shift, int *order, int scanner_endianness);
void destroy_pixel_converter(struct pixel_converter *pconv);

#endif /* _CONVERT_H_ */

#else /* CONVERT_INTERNAL */

static size_t CONVERT(struct pixel_converter *pconv,
		      uint8_t* pixels,
		      size_t count)
{
	int i, j;
	CTYPE *src = (CTYPE *) pixels;
	CTYPE *dst = (CTYPE *) pixels;
	CTYPE *buf = (CTYPE *) pconv->buf;
	int numpixels = pconv->numpixels;
	int ncomp = pconv->ncomp;
	int rd = pconv->rd;
	int *wr = pconv->wr;
	size_t N = 0;

	for (i = 0; i < count; i++) {

		/* Store a pixel, possibly swapping the endianness. */

		for (j = 0; j < ncomp; j++) {
			//DBG(DBG_info, "wr: buf[%d] = *(0x%x)\n", wr[j], (int)((uint8_t*)src-pixels));
			//buf[wr[j]] = BSWAP(*src++);
			buf[wr[j]] = *src++;
			wr[j] = (wr[j] + ncomp) % (ncomp*numpixels);
		}

		/* Return a pixel, if any are available. */
		if (pconv->sdelay >= 0) {
			for (j = 0; j < ncomp; j++) {
				//DBG(DBG_info, "rd: *(0x%x) = buf[%d]\n", (int)((uint8_t*)dst-pixels), rd + j);
				*dst++ = buf[rd + j];
			}
			rd = (rd + ncomp) % (ncomp*numpixels);
			N++;
		} else {
			pconv->sdelay += 1;
		}
	}

	pconv->rd = rd;
	return N;
}

#undef CONVERT
#undef CTYPE
#undef BSWAP

#endif /* CONVERT_INTERNAL */
