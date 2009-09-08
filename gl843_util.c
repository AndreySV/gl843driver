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
#include <string.h>
#include <ctype.h>
#include <libusb-1.0/libusb.h>
#include "gl843_util.h"

char *g_backend;
int g_level = 0;

void vprintf_dbg(int level, const char *func, int line, const char *msg, ...)
{
	va_list ap;

	if (level > g_level)
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
	int i;

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
		if (!s)
			return;
		g_level = atoi(s);
	} else {
		g_level = level;
	}

	DBG (0, "setting debug level of %s to %d.\n", backend, g_level);
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

const char *sanei_libusb_strerror(int errcode)
{
	switch (errcode) {
	case LIBUSB_SUCCESS:
		return "Success (no error)";
	case LIBUSB_ERROR_IO:
		return "Input/output error";
	case LIBUSB_ERROR_INVALID_PARAM:
		return "Invalid parameter";
	case LIBUSB_ERROR_ACCESS:
		return "Access denied (insufficient permissions)";
	case LIBUSB_ERROR_NO_DEVICE:
		return "No such device (it may have been disconnected)";
	case LIBUSB_ERROR_NOT_FOUND:
		return "Entity not found";
	case LIBUSB_ERROR_BUSY:
		return "Resource busy";
	case LIBUSB_ERROR_TIMEOUT:
		return "Operation timed out";
	case LIBUSB_ERROR_OVERFLOW:
		return "Overflow";
	case LIBUSB_ERROR_PIPE:
		return "Pipe error";
	case LIBUSB_ERROR_INTERRUPTED:
		return "System call interrupted (perhaps due to signal)";
	case LIBUSB_ERROR_NO_MEM:
		return "Insufficient memory";
	case LIBUSB_ERROR_NOT_SUPPORTED:
		return "Operation not supported or unimplemented on this platform";
	case LIBUSB_ERROR_OTHER:
		return "Other error";
	default:
		return "Unknown libusb-1.0 error code";
	}
}

const char *sanei_strerror(int errcode)
{
	switch (errcode) {
	case SANE_STATUS_GOOD:
		return "everything A-OK";
	case SANE_STATUS_UNSUPPORTED:
		return "operation is not supported";
	case SANE_STATUS_CANCELLED:
		return "operation was cancelled";
	case SANE_STATUS_DEVICE_BUSY:
		return "device is busy; try again later";
	case SANE_STATUS_INVAL:
		return "data is invalid (includes no dev at open)";
	case SANE_STATUS_EOF:
		return "no more data available (end-of-file)";
	case SANE_STATUS_JAMMED:
		return "document feeder jammed";
	case SANE_STATUS_NO_DOCS:
		return "document feeder out of documents";
	case SANE_STATUS_COVER_OPEN:
		return "scanner cover is open";
	case SANE_STATUS_IO_ERROR:
		return "error during device I/O";
	case SANE_STATUS_NO_MEM:
		return "out of memory";
	case SANE_STATUS_ACCESS_DENIED:
		return "access to resource has been denied";
	default:
		return "undefined SANE error";
	}
}
