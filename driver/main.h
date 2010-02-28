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

#ifndef _MAIN_H_
#define _MAIN_H_

/* Type declarations for main.c */

enum Scanner_Option
{
	OPT_NUM_OPTS = 0,

	OPT_MODE_GROUP,
	OPT_MODE,
	OPT_SOURCE,
	OPT_BIT_DEPTH,
	OPT_RESOLUTION,

	OPT_GEOMETRY_GROUP,
	OPT_TL_X,
	OPT_TL_Y,
	OPT_BR_Y,
	OPT_BR_X,

	OPT_ENHANCEMENT_GROUP,
	OPT_CUSTOM_GAMMA,
	OPT_GAMMA_VECTOR,
	OPT_GAMMA_VECTOR_R,
	OPT_GAMMA_VECTOR_G,
	OPT_GAMMA_VECTOR_B,

	OPT_NUM_OPTIONS,
};

/* Option_Value union - Used to access option values given to the backend */
#ifndef SANE_OPTION
typedef union
{
	SANE_Bool b;          /**< bool */
	SANE_Word w;          /**< word */
	SANE_Word *wa;        /**< word array */
	SANE_String s;        /**< string */
}
Option_Value;
#define SANE_OPTION 1
#endif

typedef struct
{
	SANE_String_Const vendor;
	SANE_String_Const model;
	SANE_String_Const type;
	SANE_Int vid;
	SANE_Int pid;
	SANE_String_Const name;

} Scanner_Model;

/* Private extension of SANE_Device */
typedef struct
{
	SANE_Device sane_dev;
	libusb_device *usbdev;

} SANE_USB_Device;

typedef struct
{
	struct gl843_device *hw;
	struct pixel_converter *pconv;

	SANE_Bool need_warmup;
	SANE_Bool need_shading;
	SANE_Bool is_scanning;

	SANE_Option_Descriptor opt[OPT_NUM_OPTIONS];

	/* Lamp settings */

	const SANE_Int *sources;
	const SANE_String_Const *source_names;
	enum gl843_lamp source;	/* Light source */
	SANE_Range lamp_to_lim;	/* Timeout limit */
	SANE_Int lamp_timeout;	/* Lamp timeout [minutes] */

	/* Platen format */

	SANE_Range x_limit;	/* Platen left/right edges [mm] */
	SANE_Range y_limit;	/* Platen top/bottom edges [mm] */
	SANE_Fixed x_start;	/* Platen left edge offset from CCD start */
	SANE_Fixed y_start;	/* Platen top edge offset from home position */
	SANE_Fixed y_calpos;	/* White calibration position [mm] */

	/* Transparency adapter format */

	SANE_Range x_limit_ta;	/* TA left/right edges [mm] */
	SANE_Range y_limit_ta;	/* TA top/bottom edges [mm] */
	SANE_Fixed x_start_ta;	/* TA left edge offset from CCD start */
	SANE_Fixed y_start_ta;	/* TA top edge offset from home position */
	SANE_Fixed y_calpos_ta;	/* White calibration position [mm] */

	/* Current scan area */

	SANE_Range x_scan_lim;	/* Scan area left/right edges [mm] */
	SANE_Range y_scan_lim;	/* Scan area top/bottom edges [mm] */
	SANE_Fixed tl_x;	/* Current scan area left edge [mm] */
	SANE_Fixed tl_y;	/* Current scan area top edge [mm] */
	SANE_Fixed br_x;	/* Current scan area right edge [mm] */
	SANE_Fixed br_y;	/* Current scan area bottom edge [mm] */

	/* Current image format */

	const SANE_Int *modes;
	const SANE_String_Const *mode_names;
	SANE_Frame mode;	/* Color mode */
	const SANE_Int *bit_depths;
	SANE_Int depth;		/* Bits per channel */
	const SANE_Int *resolutions;
	SANE_Int dpi;		/* Scan resolution [dots per inch] */

	struct scan_setup setup; /* Scanner setup for current image format */
	int bytes_left;		/* Bytes left to read by the SANE frontend */

	/* Gamma correction tables */

	SANE_Bool use_gamma;	/* Gamma correction enabled */
	SANE_Range gamma_range;
	SANE_Word gamma_len;	/* Number of table entries, typically 256 */
	SANE_Word *gray_gamma;
	SANE_Word *red_gamma;
	SANE_Word *green_gamma;
	SANE_Word *blue_gamma;

	SANE_Range bw_range;
	SANE_Fixed bw_threshold;	/* Black/white threshold [percent] */
	SANE_Fixed bw_hysteresis;	/* Threshold hysteresis [percent] */

	/* AFE calibration and shading correction */

	struct calibration_info *calinfo;
	struct calibration_info *calinfo_ta;


} CS4400F_Scanner;

#endif /* _MAIN_H_ */
