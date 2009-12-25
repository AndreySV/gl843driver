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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sane/sane.h>
#include <sane/saneopts.h>

#include "sanei.h"

/** Option_Value union
 *  *
 *   * Convenience union to access option values given to the backend
 *    */
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


#include "util.h"
#include "low.h"
#include "cs4400f.h"

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
	libusb_device *usb_dev;

} SANE_USB_Device;

/* List of scanners recognized by the driver */
const Scanner_Model g_known_models[] =
{
	{
		.vendor = "Canon",
		.model = "CanonScan 4400F",
		.type = SANE_I18N("flatbed scanner"),
		.vid = 0x04a9, .pid = 0x2228,
		.name = "cs4400f",
	},
	{
		/* End-of-list marker */
		"", "", "", 0, 0, "",
	}
};

enum scanner_state
{
	STATE_UNAVAILABLE = 0,	/* Not connected or powered down */
	STATE_RESET,		/* Connected but not configured */
	STATE_CONFIGURED,	/* Connected and configured */
	STATE_WARMING_UP_LAMP,	/* Waiting for lamp to warm up */
	STATE_CALIBRATING,	/* Performing calibration scans */
	STATE_READY,		/* At home, configured, calibrated, waiting */
	STATE_MOVING_OUT,	/* Moving to scan start */
	STATE_SCANNING,
	STATE_MOVING_HOME,	/* Going home */
};

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

/* CanoScan 4400F properties */

#define CS4400F_VID 0x04a9
#define CS4400F_PID 0x2228

#define SANE_VALUE_SCAN_SOURCE_PLATEN	SANE_I18N("Flatbed")
#define SANE_VALUE_SCAN_SOURCE_TA	SANE_I18N("Transparency Adapter")

const SANE_Int cs4400f_sources[] = { 2, LAMP_PLATEN, LAMP_TA };
const SANE_String_Const cs4400f_source_names[] = {
	SANE_VALUE_SCAN_SOURCE_PLATEN, SANE_VALUE_SCAN_SOURCE_TA, NULL };
const SANE_Int cs4400f_modes[] = { 2, SANE_FRAME_GRAY, SANE_FRAME_RGB };
const SANE_String_Const cs4400f_mode_names[] = {
	SANE_VALUE_SCAN_MODE_GRAY, SANE_VALUE_SCAN_MODE_COLOR, NULL };
const SANE_Int cs4400f_bit_depths[]  = { 2, 8, 16 };
const SANE_Int cs4400f_resolutions[] = { 5, 75, 150, 300, 600, 1200 };
const SANE_Range cs4400f_x_limit     = { SANE_FIX(0.0), SANE_FIX(210.0), 0 };
const SANE_Range cs4400f_y_limit     = { SANE_FIX(0.0), SANE_FIX(297.0), 0 };
const SANE_Fixed cs4400f_y_calpos    = SANE_FIX(5.0);
const SANE_Range cs4400f_x_limit_ta  = { SANE_FIX(0.0), SANE_FIX(100.0), 0 };
const SANE_Range cs4400f_y_limit_ta  = { SANE_FIX(0.0), SANE_FIX(100.0), 0 };
const SANE_Fixed cs4400f_y_calpos_ta = SANE_FIX(5.0);

typedef struct
{
	enum scanner_state state;
	struct gl843_device *hw_dev;

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
	SANE_Fixed y_calpos;	/* White calibration position [mm] */

	/* Transparency adapter format */

	SANE_Range x_limit_ta;	/* TA left/right edges [mm] */
	SANE_Range y_limit_ta;	/* TA top/bottom edges [mm] */
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
	SANE_Int x_dpi;		/* Horizontal resolution [dots per inch] */
	SANE_Int y_dpi;		/* Vertical resolution [dots per inch] */

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

} CS4400F_Scanner;

/* Backend globals */

static libusb_context *g_libusb_ctx;
static SANE_USB_Device **g_scanners;
extern int g_dbg_level; /* util.c */

/* Functions */

static size_t max_string_size(const SANE_String_Const *strings)
{
	size_t size, max_size = 0;

	for (; *strings != NULL; strings++) {
		size = strlen(*strings) + 1;
		if (size > max_size)
			max_size = size;
	}
	return max_size;
}

/* Get index of constraint s in string list */
static int find_constraint_string(SANE_String s, const SANE_String_Const *strings)
{
	int i;
	for (i = 0; *strings != NULL; strings++) {
		if (strcmp(s, *strings) == 0)
			return i;
	}
	DBG(DBG_error0, "BUG: unknown constraint string %s\n", s);
	return 0;
}

/* Get index of constraint v in word array */
static int find_constraint_value(SANE_Word v, const SANE_Word *values)
{
	int i, N;
	N = *values++;
	for (i = 1; i <= N; i++) {
		if (v == *values++)
			return i;
	}
	DBG(DBG_error0, "BUG: unknown constraint value %d\n", v);
	return 1;
}

static SANE_Word *create_gamma(int N, float gamma)
{
	int k;
	SANE_Word *g;

	if (gamma < 0.01)
		gamma = 0.01;
	g = malloc(N * sizeof(SANE_Word));
	if (!g)
		return NULL;

	for (k = 0; k < N; k++) {
		g[k] = (uint16_t) (65535 * powf((float)k / N, 1/gamma) + 0.5);
	}
	return g;
}

static void destroy_CS4400F(CS4400F_Scanner *s)
{
	if (!s)
		return;

	free(s->gray_gamma);
	free(s->red_gamma);
	free(s->green_gamma);
	free(s->blue_gamma);

	/* TODO: Destroy gl843_device */

	memset(s, 0, sizeof(*s));
	free(s);
}

CS4400F_Scanner *create_CS4400F(unsigned int bus, unsigned int adr)
{
	int i;
	const int gamma_len = 256;
	float default_gamma = 1.0;
	SANE_Option_Descriptor *opt;
	CS4400F_Scanner *s;

	CHK_MEM(s = calloc(sizeof(CS4400F_Scanner), 1));

	/** Scanner properties **/

	s->sources      = cs4400f_sources;
	s->source_names = cs4400f_source_names;
	s->modes        = cs4400f_modes;
	s->mode_names   = cs4400f_mode_names;
	s->bit_depths   = cs4400f_bit_depths;
	s->resolutions  = cs4400f_resolutions;

	s->x_limit      = cs4400f_x_limit;
	s->y_limit      = cs4400f_y_limit;
	s->y_calpos     = cs4400f_y_calpos;

	s->x_limit_ta   = cs4400f_x_limit_ta;
	s->y_limit_ta   = cs4400f_y_limit_ta;
	s->y_calpos_ta  = cs4400f_y_calpos_ta;

	/** Scanning settings **/

	s->source = LAMP_PLATEN;
	s->lamp_to_lim = (SANE_Range){ 0, 15, 0 };
	s->lamp_timeout = 4;

	s->x_scan_lim.min = SANE_FIX(0.0);
	s->x_scan_lim.max = s->x_limit.max - s->x_limit.min,
	s->x_scan_lim.quant = 0;

	s->y_scan_lim.min = SANE_FIX(0.0);
	s->y_scan_lim.max = s->y_limit.max - s->y_limit.min,
	s->y_scan_lim.quant = 0;

	s->tl_x = SANE_FIX(0.0);
	s->tl_y = SANE_FIX(0.0);
	s->br_x = s->x_scan_lim.max;
	s->br_y = s->y_scan_lim.max;

	s->mode = SANE_FRAME_RGB;
	s->depth = 16;

	s->x_dpi = 300;
	s->y_dpi = 300;

	s->use_gamma = SANE_FALSE;
	s->gamma_range  = (SANE_Range){ 0, 65535, 0 };
	s->gamma_len = gamma_len;
	CHK_MEM(s->gray_gamma = create_gamma(gamma_len, default_gamma));
	CHK_MEM(s->red_gamma = create_gamma(gamma_len, default_gamma));
	CHK_MEM(s->green_gamma = create_gamma(gamma_len, default_gamma));
	CHK_MEM(s->blue_gamma = create_gamma(gamma_len, default_gamma));

	s->bw_range = (SANE_Range){ SANE_FIX(0.0), SANE_FIX(100.0), 0 };
	s->bw_threshold = SANE_FIX(50.0);
	s->bw_hysteresis = SANE_FIX(0.0);

	/** SANE options **/

	for (i = 0; i < OPT_NUM_OPTIONS; i++)
		s->opt[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;

	/* Number of options */

	opt = s->opt + OPT_NUM_OPTS;

	opt->title = SANE_TITLE_NUM_OPTIONS;
	opt->desc = SANE_DESC_NUM_OPTIONS;
	opt->type = SANE_TYPE_INT;
	opt->size = sizeof(SANE_Word);
	opt->cap = SANE_CAP_SOFT_DETECT;

	/* Standard options */

	opt = s->opt + OPT_MODE_GROUP;

	opt->title = SANE_TITLE_STANDARD;
	opt->desc = SANE_DESC_STANDARD;
	opt->type = SANE_TYPE_GROUP;
	opt->size = sizeof(SANE_Word);
	opt->cap = 0;
	opt->constraint_type = SANE_CONSTRAINT_NONE;

	/* color modes */

	opt = s->opt + OPT_MODE;

	opt->name = SANE_NAME_SCAN_MODE;
	opt->title = SANE_TITLE_SCAN_MODE;
	opt->desc = SANE_DESC_SCAN_MODE;
	opt->type = SANE_TYPE_STRING;
	opt->size = max_string_size(s->mode_names);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_STRING_LIST;
	opt->constraint.string_list = s->mode_names;

	/* sources */

	opt = s->opt + OPT_SOURCE;

	opt->name  = SANE_NAME_SCAN_SOURCE;
	opt->title = SANE_TITLE_SCAN_SOURCE;
	opt->desc = SANE_DESC_SCAN_SOURCE;
	opt->type = SANE_TYPE_STRING;
	opt->size = max_string_size(s->source_names);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_STRING_LIST;
	opt->constraint.string_list = s->source_names;

	/* bit depths */

	opt = s->opt + OPT_BIT_DEPTH;

	opt->name  = SANE_NAME_BIT_DEPTH;
	opt->title = SANE_TITLE_BIT_DEPTH;
	opt->desc = SANE_DESC_BIT_DEPTH;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_BIT;
	opt->size = sizeof(SANE_Word);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_WORD_LIST;
	opt->constraint.word_list = s->bit_depths;

	/* resolutions */

	opt = s->opt + OPT_RESOLUTION;

	opt->name = SANE_NAME_SCAN_RESOLUTION;
	opt->title = SANE_TITLE_SCAN_RESOLUTION;
	opt->desc = SANE_DESC_SCAN_RESOLUTION;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_DPI;
	opt->size = sizeof(SANE_Word);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_WORD_LIST;
	opt->constraint.word_list = s->resolutions;

  	/* Geometry options */

	opt = s->opt + OPT_GEOMETRY_GROUP;

	opt->title = SANE_TITLE_GEOMETRY;
	opt->desc = SANE_DESC_GEOMETRY;
	opt->type = SANE_TYPE_GROUP;
	opt->size = sizeof(SANE_Word);
	opt->cap = 0;
	opt->constraint_type = SANE_CONSTRAINT_NONE;

	/* left */

	opt = s->opt + OPT_TL_X;

	opt->name = SANE_NAME_SCAN_TL_X;
	opt->title = SANE_TITLE_SCAN_TL_X;
	opt->desc = SANE_DESC_SCAN_TL_X;
	opt->type = SANE_TYPE_FIXED;
	opt->size = sizeof(SANE_Fixed);
	opt->cap |= 0;
	opt->unit = SANE_UNIT_MM;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->x_limit;

	/* top */

	opt = s->opt + OPT_TL_Y;

	opt->name = SANE_NAME_SCAN_TL_Y;
	opt->title = SANE_TITLE_SCAN_TL_Y;
	opt->desc = SANE_DESC_SCAN_TL_Y;
	opt->type = SANE_TYPE_FIXED;
	opt->unit = SANE_UNIT_MM;
	opt->size = sizeof(SANE_Fixed);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->y_limit;

	/* right */

	opt = s->opt + OPT_BR_X;

	opt->name = SANE_NAME_SCAN_BR_X;
	opt->title = SANE_TITLE_SCAN_BR_X;
	opt->desc = SANE_DESC_SCAN_BR_X;
	opt->type = SANE_TYPE_FIXED;
	opt->unit = SANE_UNIT_MM;
	opt->size = sizeof(SANE_Fixed);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->x_limit;

	/* bottom */

	opt = s->opt + OPT_BR_Y;

	opt->name = SANE_NAME_SCAN_BR_Y;
	opt->title = SANE_TITLE_SCAN_BR_Y;
	opt->desc = SANE_DESC_SCAN_BR_Y;
	opt->type = SANE_TYPE_FIXED;
	opt->unit = SANE_UNIT_MM;
	opt->size = sizeof(SANE_Fixed);
	opt->cap |= 0;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->y_limit;

	/* Enhancement options */

	opt = s->opt + OPT_ENHANCEMENT_GROUP;

	opt->title = SANE_TITLE_ENHANCEMENT;
	opt->desc = SANE_DESC_ENHANCEMENT;
	opt->type = SANE_TYPE_GROUP;
	opt->size = 0;
	opt->cap = SANE_CAP_ADVANCED;
	opt->constraint_type = SANE_CONSTRAINT_NONE;

	/* enable/disable gamma correction */

	opt = s->opt + OPT_CUSTOM_GAMMA;

	opt->name = SANE_NAME_CUSTOM_GAMMA;
	opt->title = SANE_TITLE_CUSTOM_GAMMA;
	opt->desc = SANE_DESC_CUSTOM_GAMMA;
	opt->type = SANE_TYPE_BOOL;
	opt->cap |= SANE_CAP_ADVANCED;

	/* grayscale gamma vector */

	opt = s->opt + OPT_GAMMA_VECTOR;

	opt->name = SANE_NAME_GAMMA_VECTOR;
	opt->title = SANE_TITLE_GAMMA_VECTOR;
	opt->desc = SANE_DESC_GAMMA_VECTOR;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_NONE;
	opt->size = s->gamma_len * sizeof(SANE_Word);
	opt->cap |= SANE_CAP_INACTIVE | SANE_CAP_ADVANCED;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->gamma_range;

	/* red gamma vector */

	opt = s->opt + OPT_GAMMA_VECTOR_R;

	opt->name = SANE_NAME_GAMMA_VECTOR_R;
	opt->title = SANE_TITLE_GAMMA_VECTOR_R;
	opt->desc = SANE_DESC_GAMMA_VECTOR_R;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_NONE;
	opt->size = s->gamma_len * sizeof(SANE_Word);
	opt->cap |= SANE_CAP_INACTIVE | SANE_CAP_ADVANCED;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->gamma_range;

	/* green gamma vector */

	opt = s->opt + OPT_GAMMA_VECTOR_G;

	opt->name = SANE_NAME_GAMMA_VECTOR_G;
	opt->title = SANE_TITLE_GAMMA_VECTOR_G;
	opt->desc = SANE_DESC_GAMMA_VECTOR_G;
	opt->type = SANE_TYPE_INT;
	opt->unit = SANE_UNIT_NONE;
	opt->size = s->gamma_len * sizeof(SANE_Word);
	opt->cap |= SANE_CAP_INACTIVE | SANE_CAP_ADVANCED;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->gamma_range;

	/* blue gamma vector */

	opt = s->opt + OPT_GAMMA_VECTOR_B;

	opt->name = SANE_NAME_GAMMA_VECTOR_B;
	opt->title = SANE_TITLE_GAMMA_VECTOR_B;
	opt->desc = SANE_DESC_GAMMA_VECTOR_B;
	opt->type = SANE_TYPE_INT;
	opt->size = s->gamma_len * sizeof(SANE_Word);
	opt->cap |= SANE_CAP_INACTIVE | SANE_CAP_ADVANCED;
	opt->unit = SANE_UNIT_NONE;
	opt->constraint_type = SANE_CONSTRAINT_RANGE;
	opt->constraint.range = &s->gamma_range;

	return s;

chk_mem_failed:
	DBG(DBG_error0, "Out of memory\n");
	destroy_CS4400F(s);
	return NULL;
}

static void free_sane_usb_dev(SANE_USB_Device *s)
{
	if (!s)
		return;

	libusb_unref_device(s->usb_dev);
	free((void*)s->sane_dev.name);
	memset(s, 0, sizeof(*s));
	free(s);
}

static SANE_USB_Device *mk_sane_usb_dev(const Scanner_Model *model,
					libusb_device *usbdev)
{
	int ret;
	SANE_USB_Device *d;

	CHK_MEM(d = calloc(sizeof(*d), 1));

	CHK(asprintf((char **) &d->sane_dev.name, "%s:%03d:%03d", model->name,
		libusb_get_bus_number(usbdev), libusb_get_device_address(usbdev)));

	d->sane_dev.vendor = model->vendor;
	d->sane_dev.model = model->model;
	d->sane_dev.type = model->type;
	d->usb_dev = libusb_ref_device(usbdev);
	return d;

chk_failed:
chk_mem_failed:
	free_sane_usb_dev(d);
	return NULL;
}

static void free_sane_usb_devs(SANE_USB_Device **devs)
{
	SANE_USB_Device **d;

	if (!devs)
		return;

	for (d = devs; *d != NULL; d++)
		free_sane_usb_dev(*d);

	free(devs);
}

#ifndef DRIVER_BUILD
#error DRIVER_BUILD number is undefined
#endif

SANE_Status sane_init(SANE_Int* version_code,
		      SANE_Auth_Callback authorize)
{
	int ret;

	if (version_code)
		*version_code = SANE_VERSION_CODE(1,0,DRIVER_BUILD);

	g_scanners = NULL;

	init_debug("GL843", -1);
	ret = libusb_init(&g_libusb_ctx);
	if (ret != LIBUSB_SUCCESS) {
		DBG(DBG_error0, "Cannot initialize libusb: %s",
			sanei_libusb_strerror(ret));
		return SANE_STATUS_IO_ERROR;
	}

	if (g_dbg_level > 0)
		libusb_set_debug(g_libusb_ctx, 2);

	return SANE_STATUS_GOOD;
}

void sane_exit()
{
	if (g_libusb_ctx) {
		libusb_exit(g_libusb_ctx);
		g_libusb_ctx = NULL;
	}
	free_sane_usb_devs(g_scanners);
}

SANE_Status sane_get_devices(const SANE_Device ***device_list,
			     SANE_Bool local_only)
{
	int ret;
	int i, n;
	struct libusb_device **usbdevs = NULL;

	/* Clear current device list */

	free_sane_usb_devs(g_scanners);
	CHK_MEM(g_scanners = calloc(sizeof(SANE_USB_Device*), 1));

	/* Scan all USB devices */

	CHK(libusb_get_device_list(g_libusb_ctx, &usbdevs));

	n = 0;
	for (i = 0; usbdevs[i] != NULL; i++) {

		struct libusb_device_descriptor dd;
		SANE_USB_Device **tmp;
		const Scanner_Model *m;

		/* Find matching scanner model */

		CHK(libusb_get_device_descriptor(usbdevs[i], &dd));

		for (m = g_known_models; m->vid != 0; m++) {
			if (m->vid == dd.idVendor && m->pid == dd.idProduct)
				break;
		}
		if (m->vid == 0)
			continue;

		/* Enumerate found scanner */

		DBG(DBG_proc, "found USB device 0x%04x:0x%04x\n",
			dd.idVendor, dd.idProduct);

		CHK_MEM(tmp = realloc(g_scanners, (n+2)*sizeof(*tmp)));
		g_scanners = tmp;
		CHK_MEM(g_scanners[n] = mk_sane_usb_dev(m, usbdevs[i]));
		n++;
	}
	g_scanners[n] = NULL; /* Add list terminator */

	if (device_list)
		*device_list = (SANE_Device **) g_scanners;

	libusb_free_device_list(usbdevs, 1);
	return SANE_STATUS_GOOD;

chk_mem_failed:
	ret = LIBUSB_ERROR_NO_MEM;
chk_failed:
	if (usbdevs)
		libusb_free_device_list(usbdevs, 1);
	free_sane_usb_devs(g_scanners);
	DBG(DBG_error, "Device enumeration failed: %s\n",
		sanei_libusb_strerror(ret));
	return SANE_STATUS_IO_ERROR;
}

SANE_Status sane_open(SANE_String_Const devicename,
		      SANE_Handle *handle)
{
	int i;
	int ret;
	SANE_USB_Device *dev = NULL;

	if (g_scanners == NULL)
		CHK_SANE(sane_get_devices(NULL, SANE_TRUE));

	/* Find scanner in device list */

	if (strlen(devicename) == 0 || strcmp(devicename, "auto") == 0) {
		dev = g_scanners[0];
	} else {
		for (i = 0; g_scanners[i] != NULL; i++) {
			if (strcmp(devicename, g_scanners[i]->sane_dev.name) == 0) {
				dev = g_scanners[i];
				break;
			}
		}
	}
	if (dev == NULL) {
		DBG(DBG_proc, "device not found\n");
		return SANE_STATUS_INVAL; /* No device found */
	}

	/* Open USB interface */

	DBG(DBG_proc, "opening %s\n", g_scanners[i]->sane_dev.name);


	/* TODO: The rest ... */

//	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
	return SANE_STATUS_UNSUPPORTED;

chk_sane_failed:
	return ret;
}

void sane_close(SANE_Handle handle)
{
//	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
}

const SANE_Option_Descriptor *sane_get_option_descriptor(SANE_Handle handle,
							 SANE_Int option)
{
	if (option < 0 || option >= OPT_NUM_OPTIONS)
		return NULL;
	return ((CS4400F_Scanner *) handle)->opt + option;
}

static void enable_option(CS4400F_Scanner *s, SANE_Int option)
{
	s->opt[option].cap &= ~SANE_CAP_INACTIVE;
}

static void disable_option(CS4400F_Scanner *s, SANE_Int option)
{
	s->opt[option].cap |= SANE_CAP_INACTIVE;
}

SANE_Status sane_control_option(SANE_Handle handle,
				SANE_Int option,
				SANE_Action action,
				void *value,
				SANE_Int *info)
{
	int i;
	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
	Option_Value *val = (Option_Value *)value;
	SANE_Int flags = 0;
	SANE_Option_Descriptor *opt;

	if (option < 0 || option >= OPT_NUM_OPTIONS)
		return SANE_STATUS_INVAL;

	opt = &(s->opt[option]);

	switch (action) {
	case SANE_ACTION_GET_VALUE:
	{
		if (value == NULL)
			return SANE_STATUS_INVAL;

		switch (option) {
		case OPT_MODE:
			i = find_constraint_value(s->mode, s->modes) - 1;
			strcpy(val->s, s->mode_names[i]);
			break;
		case OPT_SOURCE:
			i = find_constraint_value(s->source, s->sources) - 1;
			strcpy(val->s, s->source_names[i]);
			break;
		case OPT_BIT_DEPTH:
			val->w = s->depth;
			break;
		case OPT_RESOLUTION:
			val->w = s->x_dpi;
			break;
		case OPT_TL_X:
			val->w = s->tl_x;
			break;
		case OPT_TL_Y:
			val->w = s->tl_y;
			break;
		case OPT_BR_X:
			val->w = s->br_x;
			break;
		case OPT_BR_Y:
			val->w = s->br_y;
			break;
		case OPT_CUSTOM_GAMMA:
			val->w = s->use_gamma;
			break;
		case OPT_GAMMA_VECTOR:
			memcpy(value, s->gray_gamma,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_R:
			memcpy(value, s->red_gamma,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_G:
			memcpy(value, s->green_gamma,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_B:
			memcpy(value, s->blue_gamma,
				s->gamma_len * sizeof(SANE_Word));
			break;
		default:
			return SANE_STATUS_INVAL;
		}

		return SANE_STATUS_GOOD;
	}

	case SANE_ACTION_SET_VALUE:
	{
		SANE_Status ret;

		if (value == NULL)
			return SANE_STATUS_INVAL;

//		if (s->is_scanning)
//				return SANE_STATUS_DEVICE_BUSY;

		if (!SANE_OPTION_IS_ACTIVE(opt->cap)
				|| !SANE_OPTION_IS_SETTABLE(opt->cap))
			return SANE_STATUS_INVAL;

		ret = sanei_constrain_value(opt, value, info);
		if (ret != SANE_STATUS_GOOD)
			return SANE_STATUS_INVAL;

		switch (option) {
		case OPT_MODE:
			i = find_constraint_string(val->s, s->mode_names);
			s->mode = s->modes[i+1];
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_SOURCE:
			i = find_constraint_string(val->s, s->source_names);
			s->source = s->sources[i+1];
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_BIT_DEPTH:
			s->depth = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_RESOLUTION:
			s->x_dpi = val->w;
			s->y_dpi = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_TL_X:
			s->tl_x = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_TL_Y:
			s->tl_y = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_BR_X:
			s->br_x = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_BR_Y:
			s->br_y = val->w;
			flags |= SANE_INFO_RELOAD_PARAMS;
			break;
		case OPT_CUSTOM_GAMMA:
			if (val->w != s->use_gamma)
				flags |= SANE_INFO_RELOAD_OPTIONS;

			s->use_gamma = val->w;

			if (s->use_gamma && s->depth == 16) {
				s->depth = 8;
				flags |= SANE_INFO_RELOAD_PARAMS;
			}

			if (s->use_gamma && s->mode == SANE_FRAME_RGB) {
				/* Enable color gamma, disable gray gamma */
				disable_option(s, OPT_GAMMA_VECTOR);
				enable_option(s, OPT_GAMMA_VECTOR_R);
				enable_option(s, OPT_GAMMA_VECTOR_G);
				enable_option(s, OPT_GAMMA_VECTOR_B);

			} else if (s->use_gamma && s->mode == SANE_FRAME_GRAY) {
				/* Enable gray gamma, disable color gamma */
				enable_option(s, OPT_GAMMA_VECTOR);
				disable_option(s, OPT_GAMMA_VECTOR_R);
				disable_option(s, OPT_GAMMA_VECTOR_G);
				disable_option(s, OPT_GAMMA_VECTOR_B);

			} else {
				/* Disable all gamma */
				disable_option(s, OPT_GAMMA_VECTOR);
				disable_option(s, OPT_GAMMA_VECTOR_R);
				disable_option(s, OPT_GAMMA_VECTOR_G);
				disable_option(s, OPT_GAMMA_VECTOR_B);
			}
			break;
		case OPT_GAMMA_VECTOR:
			memcpy(s->gray_gamma, value,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_R:
			memcpy(s->red_gamma, value,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_G:
			memcpy(s->green_gamma, value,
				s->gamma_len * sizeof(SANE_Word));
			break;
		case OPT_GAMMA_VECTOR_B:
			memcpy(s->blue_gamma, value,
				s->gamma_len * sizeof(SANE_Word));
			break;
		default:
			return SANE_STATUS_INVAL;
		}

		if (info)
			*info |= flags;
		return SANE_STATUS_GOOD;
	}

	case SANE_ACTION_SET_AUTO:
		return SANE_STATUS_UNSUPPORTED;	/* Not used */
	default:
		return SANE_STATUS_INVAL;
	}

	/* NOTREACHED */
}

SANE_Status sane_get_parameters(SANE_Handle handle, SANE_Parameters *params)
{
	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;

	params->format = s->mode;
	params->last_frame = SANE_TRUE;
	params->pixels_per_line = mm_to_px(s->tl_x, s->br_x, s->x_dpi, NULL);
	params->bytes_per_line = (params->pixels_per_line * s->depth + 7) / 8;
	if (s->mode == SANE_FRAME_RGB)
		params->bytes_per_line *=  3;
	params->lines = mm_to_px(s->tl_y, s->br_y, s->y_dpi, NULL);
	params->depth = s->depth;

	return SANE_STATUS_GOOD;
}

SANE_Status sane_start(SANE_Handle handle)
{
//	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
	return SANE_STATUS_UNSUPPORTED;
}

SANE_Status sane_read(SANE_Handle handle,
		      SANE_Byte *data,
		      SANE_Int max_length,
		      SANE_Int *length)
{
//	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
	return SANE_STATUS_UNSUPPORTED;
}

void sane_cancel(SANE_Handle handle)
{
//	CS4400F_Scanner *s = (CS4400F_Scanner *) handle;
}

SANE_Status sane_set_io_mode(SANE_Handle handle, SANE_Bool non_blocking)
{
	if (non_blocking == SANE_FALSE)
		return SANE_STATUS_GOOD;
	return SANE_STATUS_UNSUPPORTED;
}

SANE_Status sane_get_select_fd(SANE_Handle handle, SANE_Int *fd)
{
	return SANE_STATUS_UNSUPPORTED;
}


/* DLL exports */

/* GCC 4.x attribute */
#define DLL_EXPORT __attribute__ ((visibility("default")))
#define ENTRY(name) sane_gl843_##name

DLL_EXPORT SANE_Status
ENTRY(init)(SANE_Int* version_code, SANE_Auth_Callback authorize)
{
	SANE_Status ret;
	DBG(DBG_api, "enter\n");
	ret = sane_init(version_code, authorize);
	DBG(DBG_api, "leave, status: %s\n", sanei_strerror(ret));
	return ret;
}

DLL_EXPORT void
ENTRY(exit)()
{
	DBG(DBG_api, "enter\n");
	sane_exit();
	DBG(DBG_api, "leave\n");
}

DLL_EXPORT SANE_Status
ENTRY(get_devices)(const SANE_Device ***device_list, SANE_Bool local_only)
{
	SANE_Status ret;
	DBG(DBG_api, "enter\n");
	ret = sane_get_devices(device_list, local_only);
	DBG(DBG_api, "leave, status: %s\n", sanei_strerror(ret));
	return ret;
}

DLL_EXPORT SANE_Status
ENTRY(open)(SANE_String_Const devicename, SANE_Handle *handle)
{
	SANE_Status ret;
	DBG(DBG_api, "enter\n");
	ret = sane_open(devicename, handle);
	DBG(DBG_api, "leave, status: %s\n", sanei_strerror(ret));
	return ret;
}

DLL_EXPORT
void ENTRY(close)(SANE_Handle handle)
{
	sane_close(handle);
}

DLL_EXPORT const SANE_Option_Descriptor *
ENTRY(get_option_descriptor)(SANE_Handle handle, SANE_Int option)
{
	const SANE_Option_Descriptor *ret;
	DBG(DBG_api, "enter\n");
	ret = sane_get_option_descriptor(handle, option);
	DBG(DBG_api, "leave\n");
	return ret;
}

DLL_EXPORT SANE_Status
ENTRY(control_option)(SANE_Handle handle,
		      SANE_Int option,
		      SANE_Action action,
		      void *value,
		      SANE_Int *info)
{
	SANE_Status ret;
	DBG(DBG_api, "enter\n");
	ret = sane_control_option(handle, option, action, value, info);
	DBG(DBG_api, "leave, status: %s\n", sanei_strerror(ret));
	return ret;
}

DLL_EXPORT SANE_Status
ENTRY(get_parameters)(SANE_Handle handle, SANE_Parameters *params)
{
	SANE_Status ret;
	DBG(DBG_api, "enter\n");
	ret = sane_get_parameters(handle, params);
	DBG(DBG_api, "leave, status: %s\n", sanei_strerror(ret));
	return ret;
}

DLL_EXPORT SANE_Status
ENTRY(start)(SANE_Handle handle)
{
	SANE_Status ret;
	DBG(DBG_api, "enter\n");
	ret = sane_start(handle);
	DBG(DBG_api, "leave, status: %s\n", sanei_strerror(ret));
	return ret;
}

DLL_EXPORT SANE_Status
ENTRY(read)(SANE_Handle handle,
	    SANE_Byte *data,
	    SANE_Int max_length,
	    SANE_Int *length)
{
	SANE_Status ret;
	DBG(DBG_api, "enter\n");
	ret = sane_read(handle, data, max_length, length);
	DBG(DBG_api, "leave, status: %s\n", sanei_strerror(ret));
	return ret;
}

DLL_EXPORT void
ENTRY(cancel)(SANE_Handle handle)
{
	DBG(DBG_api, "enter\n");
	sane_cancel(handle);
	DBG(DBG_api, "leave\n");
}

DLL_EXPORT SANE_Status
ENTRY(set_io_mode)(SANE_Handle handle, SANE_Bool non_blocking)
{
	SANE_Status ret;
	DBG(DBG_api, "enter\n");
	ret = sane_set_io_mode(handle, non_blocking);
	DBG(DBG_api, "leave, status: %s\n", sanei_strerror(ret));
	return ret;
}

DLL_EXPORT SANE_Status
ENTRY(get_select_fd)(SANE_Handle handle, SANE_Int *fd)
{
	SANE_Status ret;
	DBG(DBG_api, "enter\n");
	ret = sane_get_select_fd(handle, fd);
	DBG(DBG_api, "leave, status: %s\n", sanei_strerror(ret));
	return ret;
}

