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
#ifndef _SCAN_H_
#define _SCAN_H_

#include "defs.h"

struct calibration_info
{
	/* Key */

	enum gl843_lamp source;
	float cal_y_pos;
	int start_x;
	int width;
	int dpi;

	/* Data */

	uint8_t offset[3];	/* AFE offset register */
	float gain[3];		/* AFE gain */
	size_t sc_len;		/* Number of bytes in shading correction */
	uint16_t sc[0];		/* Shading correction */

	/* Used when calibrating */

	int height;		/* Number of lines to scan */
	uint16_t A;		/* Shading gain factor (0x2000 or 0x4000) */
};

int setup_motor(struct gl843_device *dev, struct scan_setup *ss);
int do_warmup_scan(struct gl843_device *dev, float y_pos);
int reset_and_move_home(struct gl843_device *dev);
int warm_up_scanner(struct gl843_device *dev, enum gl843_lamp source,
	int lamp_timeout, float cal_y_pos);
int wait_for_pixels(struct gl843_device *dev);


#endif /* _SCAN_H_ */
