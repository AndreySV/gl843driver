/* Utility functions and declarations.
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

#ifndef _GL843_UTIL_H_
#define _GL843_UTIL_H_

#include <time.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define ALIGN(v, a) (((v)+(a)-1) & ~((a)-1))
#define ALIGN_DN(v, a) ((v) & ~((a)-1))
/* Make v evenly divisible by 2^STEPTIM by rounding up */
#define STEPTIM_ALIGN_UP(v) ALIGN((v), 1 << STEPTIM)
/* Make v evenly divisible by 2^STEPTIM by rounding down */
#define STEPTIM_ALIGN_DN(v) ALIGN_DN((v), 1 << STEPTIM)

#define CHK(x)					\
	do {					\
		ret = (x);			\
		if (ret < 0)			\
			goto chk_failed;	\
	} while (0)

#define CHK_SANE(x)				\
	do {					\
		ret = (x);			\
		if (ret != SANE_STATUS_GOOD)	\
			goto chk_sane_failed;	\
	} while (0)

#define CHK_MEM(x)				\
	do {					\
		void *p;			\
		p = (void*)(x);			\
		if (p == NULL)			\
			goto chk_mem_failed;	\
	} while (0)

#define DBG_error0	0	/* unfilterable messages */
#define DBG_error	1	/* fatal errors */
#define DBG_msg		2	/* scanner workflow messages */
#define DBG_warn	3	/* warnings and non-fatal errors */
#define DBG_info	4	/* informational messages */
#define DBG_api		5	/* SANE API entry/exits */
#define DBG_trace	6	/* Driver tracing */
#define DBG_trace2	7	/* Verbose driver tracing */
#define DBG_io		8	/* io functions */
#define DBG_io2		9	/* io functions that are called very often */
#define DBG_data	10	/* log image data */

#define DBG(level, msg, ...)	\
	vprintf_dbg(level, __func__, 0, msg, ##__VA_ARGS__)

#define DBG_LN(level, msg, ...)	\
	vprintf_dbg(level, __func__, __LINE__, msg, ##__VA_ARGS__)

void vprintf_dbg(int level, const char *func, int line, const char *msg, ...)
	__attribute__ ((format (printf, 4, 5)));
void init_debug(const char *backend, int level);

struct dbg_timer {
	clockid_t clk_id;
	struct timespec res;
	struct timespec ts;
};

/* Some simple timing functions */

void init_timer(struct dbg_timer *timer, clockid_t clk_id);
void reset_timer(struct dbg_timer *timer);
double get_timer(struct dbg_timer *timer);

int native_endianness(void);
int __attribute__ ((pure)) host_is_big_endian(void);
int __attribute__ ((pure)) host_is_little_endian(void);
void swap_buffer_endianness(uint16_t *src, uint16_t *dst, int len);

int mm_to_px(SANE_Fixed start, SANE_Fixed end, int dpi, int *offset);
float __attribute__ ((pure)) satf(float v, float min, float max);

#endif /* _GL843_UTIL_H_ */
