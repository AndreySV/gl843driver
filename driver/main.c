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

#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdbool.h>

enum scanner_state
{
	STATE_UNAVAILABLE = 0,	/* Not connected or powered down */
	STATE_RESET,		/* Connected but not configured */
	STATE_CONFIGURED,	/* Connected and configured */
	STATE_WARMING_UP_LAMP,	/* Waiting for lamp to warm up */
	STATE_CALIBRATING,	/* Performing calibration scans */
	STATE_READY,		/* At home, configured, waiting */
	STATE_MOVING_OUT,	/* Moving to scan start */
	STATE_SCANNING,
	STATE_MOVING_HOME,	/* Going home */
};

enum carriage_state
{
	CARRIAGE_UNKNOWN = 0,	/* Unknown position and speed */
	CARRIAGE_HOME,		/* At home, not moving */
	CARRIAGE_MOVING_OUT,	/* Moving forward */
	CARRIAGE_MOVING_HOME,	/* Moving to the home position */
	CARRIAGE_STATIONARY	/* Away from home, not moving */
};

enum scanner_retcode
{
	SCAN_EOK = 0,
	SCAN_EAGAIN,
	SCAN_EINPROGRESS,
	SCAN_ENODEV,
	SCAN_EINVAL,
	SCAN_ENOMEM,
	SCAN_EIO,
	SCAN_EBUSY,
	SCAN_ECANCELLED,
};

struct scanner_settings
{
	enum gl843_lamp source;	/* LAMP_PLATEN, LAMP_TA */
	int lamp_timeout;	/* Lamp timeout [minutes] */
	int cal_pos;		/* White strip position [lines] */

	enum gl843_pixformat fmt; /* Pixel format / bit depth */
	int filter;		/* Monochrome channel select [1,2,3 == R,G,B] */
	int x_dpi;		/* Horizontal resolution [dots per inch] */
	int y_dpi;		/* Vertical resolution [dots per inch] */

	int x_start;		/* Scanning left pos [pixels] */
	int y_start;		/* Scanning start line [lines] */
	unsigned int width;	/* Scanning width [pixels] */
	unsigned int height;	/* Scanning height [lines] */

	float bw_threshold;	/* Black/white threshold [percent] */
	float bw_hysteresis;	/* Threshold hysteresis [percent] */

	float r_gamma, g_gamma, b_gamma;
};


typedef struct {
	struct scanner_settings config;
	struct gl843_device *hw;
	enum scanner_state state;

	bool warming_up_lamp;

} scanner_s;


static int reset_scanner(scanner_s *s)
{
	/* TODO:
	Reset
	Write base setup
	 */
}

scanner_s *scanner_open(libusb_handle *h)
{
/*
	TODOS:
	store usb handle,
	create gl843_device
	reset scanner
*/
	if (reset_scanner(s) == SCAN_EOK) {
		s->state = STATE_RESET;
	} else {
		s->state = STATE_UNAVAILABLE;
	}
	return s;
}

int scanner_configure(scanner_s *s, const struct scanner_settings *settings)
{
	// TODO: Turn off lamp if switching sources, so that warmup works properly

}

int config_horizontal()
{

}

int config_vertical()
{

}

int scanner_warmup(scanner_s *s)
{
	int ret;
	int motor_running;

	if (!is_lamp_on()) {
		s->warming_up_lamp = true;
		/* Turn on lamp, and move to white strip */
		CHK(set_lamp(s->hw, s->config.source, s->config.lamp_timeout));
		CHK(move_carriage(s->hw, s->config.cal_pos));
	}

	if (!s->warming_up_lamp)
		return EOK;	/* Assume already warmed up */
	
	CHK(motor_running = read_reg(s->hw, GL843_MOTORENB));
	if (motor_running)
		return SCAN_EAGAIN;

	scan white strip twice, one second apart and compare.
	if (white_strips_equal) {
		s->warming_up_lamp = false;
		return SCAN_EOK; // warmup complete
	else
		return SCAN_EAGAIN;
}

int scanner_calibrate(scanner_s *s)
{
}

int scanner_start(scanner_s *s)
{
}

int scanner_read(scanner_s *s, uint8_t *buf, size_t maxlen)
{
}

int scanner_get_info(scanner_s *s, struct scanner_info *info)
{
}

int scanner_cancel(scanner_s *s)
{
}

int scanner_close(scanner_s *s)
{
}

jmp_buf usb_error_env;
jmp_buf mem_error_env;

int scanner_main(scanner_s *s)
{
	int ret;
	if ((ret = setjmp(usb_error_env)) != 0)
		goto catch_usb_error;
	if ((ret = setjmp(mem_error_env)) != 0)
		goto catch_mem_error;

catch_mem_error:
	/* fatal: out of memory. */
	return 0;
catch_usb_error:
	/* TODO: First try to recover, else assume the scanner
	 * was removed or powered down.
	 */
}

int main()
{

}
