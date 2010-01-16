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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>
#include <sane/sane.h>
#include "util.h"

char *g_backend;
int g_dbg_level = 0;

void vprintf_dbg(int level, const char *func, int line, const char *msg, ...)
{
	va_list ap;

	if (level > g_dbg_level)
		return;
	if (line)
		fprintf(stderr, "[%d] %s %s:%d: ", level, g_backend, func, line);
	else
		fprintf(stderr, "[%d] %s %s: ", level, g_backend, func);

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
}

void init_debug(const char *backend, int level)
{
	char *buf, *s;
	size_t i;

	g_backend = strdup(backend);

	if (level < 0) {
		if (asprintf(&buf, "SANE_DEBUG_%s", backend) < 0) {
			fprintf(stderr, "Out of memory in %s.\n", __func__);
			exit(1);
		}
		for (i = 0; i < strlen(buf); i++)
			buf[i] = toupper(buf[i]);
		s = getenv(buf);
		free(buf);
		if (s)
			g_dbg_level = atoi(s);
	} else {
		g_dbg_level = level;
	}

	DBG (0, "setting debug level of %s to %d.\n", backend, g_dbg_level);
}

void init_timer(struct dbg_timer *timer, clockid_t clk_id)
{
	timer->clk_id = clk_id;
	clock_getres(clk_id, &timer->res);
	clock_gettime(clk_id, &timer->ts);
}

void reset_timer(struct dbg_timer *timer)
{
	clock_gettime(timer->clk_id, &timer->ts);
}

double get_timer(struct dbg_timer *timer)
{
	double diff;
	struct timespec ts2;

	clock_gettime(timer->clk_id, &ts2);

	/* Calculate elapsed time */

	ts2.tv_nsec -= timer->ts.tv_nsec;
	if (ts2.tv_nsec < 0) {
		ts2.tv_sec--;
		ts2.tv_nsec += 1000000000;
	}
	ts2.tv_sec -= timer->ts.tv_sec;

	/* Convert nanoseconds to milliseconds */

	diff = ts2.tv_sec * 1000;
	diff += ts2.tv_nsec / 1E6;

	return diff;
}

/*
* Get CPU endianness. 0 = unknown, 1 = little, 2 = big
*/
int __attribute__ ((pure)) native_endianness()
{
	/* Encoding the endianness enums in a string and then reading that
	 * string as a 32-bit int, returns the correct endianness automagically.
	 */
	return (char) *((uint32_t*)("\1\0\0\2"));
}

int __attribute__ ((pure)) host_is_big_endian()
{
	return native_endianness() == 2;
}

int __attribute__ ((pure)) host_is_little_endian()
{
	return native_endianness() == 1;
}

void swap_buffer_endianness(uint16_t *src, uint16_t *dst, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		dst[i] = ((src[i] >> 8) & 0xff) | ((src[i] & 0xff) << 8);
	}
}

/* Convert millimeters to pixels. */
int __attribute__ ((pure))
mm_to_px(SANE_Fixed start, SANE_Fixed end, int dpi, int *offset)
{
	int size;

	size = (SANE_UNFIX(end) - SANE_UNFIX(start)) / 25.4 * dpi;
	if (offset)
		*offset = SANE_UNFIX(start) / 25.4 * dpi;
	return size;
}

/* Saturate value v */
float __attribute__ ((pure)) satf(float v, float min, float max)
{
	if (v < min)
		v = min;
	else if (v > max)
		v = max;
	return v;
}

int __attribute__ ((pure)) min(int a, int b)
{
	return (a < b) ? a : b;
}

int __attribute__ ((pure)) max(int a, int b)
{
	return (a > b) ? a : b;
}

pid_t sanei_thread_begin(int (*func) (void *args), void *args)
{
	pid_t pid;
	pid = fork();
	if (pid == 0) {
		int ret;
		/* Run child process. */
		ret = func(args);
		exit(ret);
	}
	return pid;
}

