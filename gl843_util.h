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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0])

#define DBG_error0      0	/* unfilterable messages */
#define DBG_error       1	/* fatal errors */
#define DBG_init        2	/* initialization and scanning time messages */
#define DBG_warn        3	/* warnings and non-fatal errors */
#define DBG_info        4	/* informational messages */
#define DBG_proc        5	/* starting/finishing functions */
#define DBG_io          6	/* io functions */
#define DBG_io2         7	/* io functions that are called very often */
#define DBG_data        8	/* log image data */

#define DBG(level, msg, ...)	\
	vprintf_dbg(level, __func__, 0, msg, __VA_ARGS__)

#define DBG_LN(level, msg, ...)	\
	vprintf_dbg(level, __func__, __LINE__, msg, __VA_ARGS__)


void vprintf_dbg(int level, const char *func, int line, const char *msg, ...)
	__attribute__ ((format (printf, 4, 5)));
const char *sanei_libusb_strerror(int errcode);
void init_debug(const char *backend, int level);

#endif /* _GL843_UTIL_H_ */
