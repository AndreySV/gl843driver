/* Device-specific functions
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

#include <stdlib.h>
#include <stdio.h> /* printf() */
#include <stdint.h>
#include <math.h>
#include "util.h"
#include "low.h"
#include "scan.h"
#include "cs4400f.h"

/* Device-specific settings and functions for Canon Canoscan 4400F */

/* Maximum AFE gain */
float __attribute__ ((pure)) max_afe_gain()
{
	return 7.428;	/* WM8196 */
}

/* Minimum AFE gain */
float __attribute__ ((pure)) min_afe_gain()
{
	return 0.735;	/* WM8196 */
}

/* Convert AFE gain to AFE register value. */
int __attribute__ ((pure)) afe_gain_to_val(float g)
{
	g = satf(g, min_afe_gain(), max_afe_gain());
	return (int) (283 - 208/g + 0.5);	/* WM8196 */
}

int write_afe_gain(struct gl843_device *dev, int i, float g)
{
	return write_afe(dev, 40 + i, afe_gain_to_val(g));
}


/* Set static (unchanging) hardware configuration */
int do_base_configuration(struct gl843_device *dev)
{
	int ret;

	write_reg(dev, GL843_LAMPPWR, 0);

	/* SDRAM */

	struct regset_ent sdram[] = {
		/* 0x0B */
		{ GL843_CLKSET, SYSCLK_60_MHZ },	/* sometimes 48 MHz */
		{ GL843_ENBDRAM, 1 },	/* posedge => SDRAM power-on sequence */
		{ GL843_RFHDIS, 0 },	/* 0 = use auto-refresh */
		{ GL843_DRAMSEL, 1 },	/* 1 = 16Mbit */
		/* 0x9D */
		{ GL843_RAMDLY,  0 },	/* SCLK delay */
		/* 0xA2 */
		{ GL843_RFHSET, 31 },  /* refresh time [2µs] */
	};
	CHK(write_regs(dev, sdram, ARRAY_SIZE(sdram)));

	struct regset_ent gpio1[] = {

		/* CCD/CIS/ADF */

		/* 0x6B */
		{ GL843_GPOCK4, 0 },	/* 0 = pin 62 is CCD_CK4X signal */
		{ GL843_GPOCP, 0 },	/* 0 = pin 68 is CCD_CPX signal */
		{ GL843_GPOLEDB, 1 },	/* unused (no CIS) */
		{ GL843_GPOADF, 0 },	/* unused (no ADF) */

		/* Motor Vref control */

		{ GL843_GPOM13, 1 },	/* 0x6B: 1 = GPIO13 is Vref ctrl */
		{ GL843_GPOM12, 1 },	/* 0x6B: 1 = GPIO12 is Vref ctrl */
		{ GL843_GPOM11, 1 },	/* 0x6B: 1 = GPIO11 is Vref ctrl */
		{ GL843_GPOM9, 0 },	/* 0xAB: 0 = GPIO9 is GPIO */

		/* LED outputs */

		{ IOREG(0x7e), 0 },	/* GPOLED25-21,10-8 are GPIO */
	};
	CHK(write_regs(dev, gpio1, ARRAY_SIZE(gpio1)));

	CHK(write_reg(dev, IOREG(0x6e), 0xff));	/* GPOE16-9 are outputs */
	CHK(write_reg(dev, IOREG(0x6c), 1));	/* GPIO16-9 */
	CHK(write_reg(dev, IOREG(0x6f), 0));	/* GPOE8-1 are inputs */
	CHK(write_reg(dev, IOREG(0x6d), 0));	/* GPIO8-1 */
	CHK(write_reg(dev, IOREG(0xa7), 0xff));	/* GPOE24-17 are outputs */
	CHK(write_reg(dev, IOREG(0xa6), 0));	/* GPIO24-17 */
	CHK(write_reg(dev, IOREG(0xa8), 0));	/* GPOE27-25 in, GPIO27-25 = 0 */

	set_reg(dev, GL843_GPOE16, 0);
	set_reg(dev, GL843_GPOE14, 0);
	CHK(flush_regs(dev));

	struct regset_ent static_setup[] = {

		/* Frontend and CCD/CIS */

		/* 0x01 */
		{ GL843_CISSET, 0 },
		/* 0x04 */
		{ GL843_AFEMOD, 1 },	/* 1 = pixel-by-pixel color */
		{ GL843_FESET, 0 },	/* Frontend is ESIC type 1 */
		/* 0x05 */
		{ GL843_DPIHW, 3 },	/* CCD resolution = 4800 DPI */
		{ GL843_ENB20M, 0 },	/* variable pixel clock for CCD CK1 */
		{ GL843_MTLBASE, 0 },	/* CCD pixel CLK = system pixel CLK */
		/* 0x09 */
		{ GL843_EVEN1ST, 0 },	/* 0 = First line of stagger CCD is odd */
		{ GL843_SHORTTG, 0 },	/* 1 = Short SH(TG) period */
		/* 0x16 */
		{ GL843_CTRLHI, 0 },
		{ GL843_TOSHIBA, 0 },
		{ GL843_TGINV, 0 },
		{ GL843_CK1INV, 1 },
		{ GL843_CK2INV, 0 },
		{ GL843_CTRLINV, 0 },
		{ GL843_CKDIS, 1 },
		{ GL843_CTRLDIS, 1 },
		/* 0x18 */
		{ GL843_CNSET, 0 },
		{ GL843_DCKSEL, 0 },
		{ GL843_CKTOGGLE, 1 },
		{ GL843_CKDELAY, 0 },
		{ GL843_CKSEL, 0 },
		/* 0x19 */
		{ GL843_EXPDMY, 42 },
		/* 0x1A */
		{ GL843_TGLSW2, 0 },
		{ GL843_TGLSW1, 0 },
		{ GL843_MANUAL3, 1 },
		{ GL843_MANUAL1, 1 },
		{ GL843_CK4INV, 0 },
		{ GL843_CK3INV, 0 },
		{ GL843_LINECLP,0 },
		/* 0x1B */
		{ GL843_GRAYSET, 0 },
		{ GL843_CHANSEL, 0 },
		{ GL843_BGRENB, 0 },
		{ GL843_ICGENB, 0 },
		{ GL843_ICGDLY, 0 },
		/* 0x1C */
		{ GL843_CK4MTGL, 0 },
		{ GL843_CKAREA, 0 },
		/* 0x1D */
		{ GL843_CK4LOW, 0 },
		{ GL843_CK3LOW, 1 },
		{ GL843_CK1LOW, 1 },

		/* 0x34 */
		{ GL843_DUMMY, 20 },
		/* 0x59 */
		{ GL843_BSMP, 0 },
		{ GL843_BSMPW, 0 },
		/* 0x5A */
		{ GL843_ADCLKINV, 0 },
		{ GL843_RLCSEL, 1 },	/* pixel-by-pixel reset-level clamp */
		{ GL843_CDSREF, 0 },
		{ GL843_RLC, 0 },
		/* 0x7D */
		{ GL843_CK1NEG, 0 },
		{ GL843_CK3NEG, 0 },
		{ GL843_CK4NEG, 0 },
		{ GL843_RSNEG, 0 },
		{ GL843_CPNEG, 0 },
		{ GL843_BSMPNEG, 0 },
		{ GL843_VSMPNEG, 0 },
		{ GL843_DLYSET, 0 },
		/* 0x7F */
		{ GL843_BSMPDLY, 0 },	/* 0 = don't delay BSMP output */
		{ GL843_VSMPDLY, 0 },	/* 0 = don't delay VSMP output */
		/* 0x87 */
		{ GL843_ACYCNRLC, 0 },	/* */
		{ GL843_ENOFFSET, 0 },	/* */
		{ GL843_LEDADD, 0 },	/* */
		{ GL843_CK4ADC, 1 },	/* 1 = CK4MAP controls AFE MCLK */
		{ GL843_AUTOCONF, 0 },	/* unused (not CIS) */
		/* 0x9D */
		{ GL843_MULDMYLN, 0 },	/* dummy lines = LINESEL * 2^MULDMYLN */
		/* 0x9E */
		{ GL843_SEL3INV, 0 },
		/* 0xAD */
		{ GL843_CCDTYP, 0 },	/* 0,4,5 ??? */

		/* Misc */

		{ GL843_HOMENEG, 0 },	/* 0x02: home sensor polarity */
		{ GL843_BUFSEL, 16 },	/* 0x20: buffer-full threshold */
		{ GL843_BACKSCAN, 0 },	/* 0x09 */

		/* Motor */

		/* 0x09 */
		{ GL843_MCNTSET, 0 },	/* 0 = Motor table counts pixel clk */
		/* 0x66 */
		{ GL843_PHFREQ, 0 },	/* unused */
		/* 0x67 */
		{ GL843_MTRPWM, 63 },	/* No PWM (not unipolar motor) */
		/* 0x68 */
		{ GL843_FASTPWM, 63 },	/* No PWM (not unipolar motor) */
		/* 0x87 */
		{ GL843_YENB, 0 },	/* unused */
		{ GL843_YBIT, 0 },	/* unused */
		/* 0xAB */
		{ GL843_NODECEL, 0 },
		{ GL843_TB3TB1, 0 },
		{ GL843_TB5TB2, 0 },

		/* Unused signal processing features */

		/* Hardware CCD RGB-line offsets compensation.
		 * The Canoscan 4400F does not really have enough RAM,
		 * so we cannot use it. It works at 300 dpi, but not
		 * at 1200 dpi. */
		{ GL843_BLINE1ST, 1 },	/* 0x09: First CCD line is blue */
		/* 0xA0: R-to-G-to-B line displacement */
		{ GL843_LNOFSET, 0 }, /* val = y_dpi * 12 / 300 */

		/* Hardware RGB->gray conversion. Appears broken in GL843.
		 * There is pixel distortion indicating data bus contention. */
		/* 0x01 */
		{ GL843_TRUEGRAY, 0 },	/* 0 = disable */
		/* 0xA3,0xA4,0xA5 */
		{ GL843_TRUER, (int) (0.2989 * 255) },
		{ GL843_TRUEG, (int) (0.5870 * 255) },
		{ GL843_TRUEB, (int) (0.1140 * 255) },

		/* 0x08: Gamma correction related */
		{ GL843_DECFLAG, 0 },
		{ GL843_GMMFFR, 0 },
		{ GL843_GMMFFG, 0 },
		{ GL843_GMMFFB, 0 },
		{ GL843_GMMZR, 0 },
		{ GL843_GMMZG, 0 },
		{ GL843_GMMZB, 0 },

		/* Other unused functions */

		/* 0x06 */
		{ GL843_OPTEST, 0 },
		/* 0x09 */
		{ GL843_ENHANCE, 0 },
		{ GL843_NWAIT, 0 },
		/* 0x0A */
		{ GL843_LCDSEL, 0 },	/* unused (no LCD) */
		{ GL843_LCMSEL, 0 },	/* unused */
		{ GL843_ADFSEL, 0 },	/* unused (no ADF) */
		{ GL843_EPROMSEL, 0 },	/* unused (no EPROM) */
		{ GL843_RS232SEL, 0 },	/* unused (no RS232 i/f) */
		{ GL843_BAUDRAT, 0 },	/* unused (no RS232 i/f) */
		{ GL843_DOGENB, 0 },	/* 0x01 */
		{ GL843_MTLWD, 0 },	/* 0x1C */
		{ GL843_WDTIME, 2 },	/* 0x1E */
		/* 0x6B */
		{ GL843_MULTFILM, 0 },	/* unused */
		/* 0x7F */
		{ GL843_LEDCNT, 0 },	/* unused. 0 = disable LED blinking */
		/* 0x94 */
		{ GL843_MTRPLS, 255 },	/* unused (no ADF) */
		/* 0x9D */
		{ GL843_MOTLAG, 0 },	/* unused (no ADF) */
		{ GL843_CMODE, 0 },	/* unused (no RS232 i/f or LCD) */
		{ GL843_IFRS, 0 },	/* unused (no LCM) */
		/* 0xAB */
		{ GL843_FIX16CLK, 0 },	/* unknown */
		/* 0xAD */
		{ GL843_ADFTYP, 0 },	/* unused (no ADF) */
		/* 0xAE */
		{ GL843_MOTSET, 0 },	/* unused (unknown) */
		{ GL843_PROCESS, 0 },	/* unused (unknown) */
		/* 0xAF: GL843_SCANTYP, GL843_FEDTYP, GL843_ADFMOVE, */
		{ IOREG(0xaf), 0 },	/* unused (no ADF) */
	};
	CHK(write_regs(dev, static_setup, ARRAY_SIZE(static_setup)));

	/* Init the AFE, a WM8196 */

	CHK(write_afe(dev, 1, 0x23));
	CHK(write_afe(dev, 2, 0x24));
	CHK(write_afe(dev, 3, 0x2f)); /* Can be 0x1f or 0x2f */
#if 0
	/* Startup RGB offsets for Canoscan 4400F */
	CHK(write_afe(dev, 32, 96));
	CHK(write_afe(dev, 33, 96));
	CHK(write_afe(dev, 34, 96));
	/* Startup RGB gains for Canoscan 4400F */
	CHK(write_afe(dev, 40, 75));
	CHK(write_afe(dev, 41, 75));
	CHK(write_afe(dev, 42, 75));
#endif
	CHK(write_afe(dev, 32, 112));
	CHK(write_afe(dev, 33, 112));
	CHK(write_afe(dev, 34, 112));
	CHK(write_afe(dev, 40, 216));
	CHK(write_afe(dev, 41, 216));
	CHK(write_afe(dev, 42, 216));

	set_lamp(dev, LAMP_OFF, 0);
	CHK(flush_regs(dev));

	set_reg(dev, GL843_PWRBIT, 1);	/* 0x06 */
	flush_regs(dev);
	ret = 0;
chk_failed:
	return ret;
}

/* Enable or disable the scanner lamp.
 *
 * state:   Lamp state. LAMP_OFF = turn off the lamp
 *                      LAMP_PLATEN = turn on the flatbed lamp
 *                      LAMP_TA = turn on the transparency adapter lamp
 * timeout: timeout, 0 - 15 minutes. 0 = no timeout, >15 => 15 minutes.
 */
int set_lamp(struct gl843_device *dev, enum gl843_lamp state, int timeout)
{
	int ret;

	struct regset_ent lamp1[] = {
		{ GL843_MTLLAMP, 0 },	/* 0x05: timeout = LAMPTIM * 2^MTLLAMP */
		{ GL843_LPWMEN, 0 },	/* 0x0A: 0 = Disable lamp PWM */
		{ GL843_ONDUR, 159 },	/* 0x98,0x99 */
		{ GL843_OFFDUR, 175 },	/* 0x9A,0x9B */
	};
	CHK(write_regs(dev, lamp1, ARRAY_SIZE(lamp1)));

	if (timeout < 0)
		timeout = 0;
	if (timeout > 15)
		timeout = 15;

	struct regset_ent lamp2[] = {
		/* 0x03 */
		{ GL843_LAMPDOG, (timeout != 0) },
		{ GL843_XPASEL, (state == LAMP_TA) },
		{ GL843_LAMPPWR, (state != LAMP_OFF) },
		{ GL843_LAMPTIM, timeout },
	};
	CHK(write_regs(dev, lamp2, ARRAY_SIZE(lamp2)));

	ret = 0;
chk_failed:
	return ret;
}

int setup_ccd_and_afe(struct gl843_device *dev,
		      enum gl843_pixformat fmt,
		      unsigned int start_x,
		      unsigned int width,
		      int dpi,
		      int afe_dpi,
		      int linesel,
		      int tgtime, int lperiod,
		      int expr, int expg, int expb)
{
	int ret;

	int tgw, tgshld;
	int ck1map, ck3map, ck4map;
	int cph, cpl, rsh, rsl;
	int ck1mtgl, ck3mtgl;
	int vsmp;
	int rhi, rlow, ghi, glow, bhi, blow;
	int strpixel, endpixel, maxwd, scanmod;
	int deep_color, mono, use_gamma;

	/* afe_dpi = resolution "seen" by the A/D converter,
	   1:1, 1:2, 1:4 of the CCD resolution */

	if (afe_dpi == 1200) {

		tgw = 10;
		tgshld = 11;
		//tgtime = 0;		// CCD line period = LPERIOD

		ck1map = 0xf838;	// 0b1111100000111000 (63544)
		ck3map = 0xfc00;	// 0b1111110000000000 (64512)
		ck4map = 0x92a4;	// 0b1001001010100100 (37540)

		ck1mtgl = 0;
		ck3mtgl = 0;

		cph = 1; cpl = 3;
		rsh = 0; rsl = 2;

		vsmp = 11;
		rhi = 10; rlow = 13;
		ghi = 0; glow = 3;
		bhi = 6; blow = 8;

	} else if (afe_dpi == 2400) {

		tgw = 21;
		tgshld = 21;
		tgtime = 1;		// CCD line period = LPERIOD * 2

		ck1map = 0xff00;	// 0b1111111100000000 (65280)
		ck3map = 0xff00;	// 0b1111111100000000 (65280)
		ck4map = 0x5492;	// 0b0101010010010010 (21650)

		ck1mtgl = 0;
		ck3mtgl = 0;

		cph = 2; cpl = 4; rsh = 0; rsl = 2;

		vsmp = 10;
		rhi = 11; rlow = 13; ghi = 0; glow = 3; bhi = 6; blow = 9;

	} else if (afe_dpi == 4800) {

		tgw = 21;
		tgshld = 21;
		tgtime = 1;		// CCD line period = LPERIOD * 2

		ck1map = 0xffff;	// 0b1111111111111111 (65535)
		ck3map = 0xffff;	// 0b1111111111111111 (65535)
		ck4map = 0x5492;	// 0b0101010010010010 (21650)

		ck1mtgl = 1;
		ck3mtgl = 1;

		cph = 10; cpl = 12; rsh = 8; rsl = 10;

		vsmp = 3;
		rhi = 2; rlow = 5; ghi = 8; glow = 11; bhi = 13; blow = 15;
	}

	strpixel = tgw * 32 + 2 * tgshld * 32 + start_x;
	endpixel = strpixel + width;
	DBG(DBG_info, "strpixel = %d, endpixel = %d\n", strpixel, endpixel);

	switch (fmt) {
	case PXFMT_LINEART:	/* 1 bit per pixel, black and white */
	 	maxwd = ALIGN(width, 8) >> 3;
		scanmod = 0;
		mono = 1;
		break;
	case PXFMT_GRAY8:	/* 8 bits per pixel, grayscale */
		maxwd = width;
		scanmod = 0;
		mono = 1;
		break;
	case PXFMT_GRAY16:	/* 16 bits per pixel, grayscale */
		maxwd = width;
		scanmod = 7;
		mono = 1;
		break;
	case PXFMT_RGB8:	/* 24 bits per pixel, RGB color */
		maxwd = width;
		scanmod = 7;
		mono = 0;
		break;
	case PXFMT_RGB16:	/* 48 bits per pixel, RGB color */
		maxwd = width;
		scanmod = 7;
		mono = 0;
		break;
	default:
		DBG(DBG_error0, "BUG: Undefined pixel format\n");
	}

	deep_color = (fmt == PXFMT_GRAY16 || fmt == PXFMT_RGB16);
	use_gamma = (fmt != PXFMT_GRAY16 && fmt != PXFMT_RGB16);

	DBG(DBG_info, "maxwd = %d, monochrome = %d, deep_color = %d, "
		"use_gamma = %d, dpi = %d\n", maxwd, mono, deep_color,
		use_gamma, dpi);

	/* CCD and AFE settings */
	struct regset_ent frontend[] = {
		/* 0x04 */
		{ GL843_BITSET, deep_color },
		{ GL843_FILTER, mono ? 2 : 0 },	/* 0 = color, 1,2,3 = R,G,B */
		/* 0x06 */
		{ GL843_SCANMOD, scanmod },	/* 0 = 12 clks/px (24bit) */
						/* 7 = 16 clks/px (48bit) */
		/* 0x10,0x11,0x12,0x13,0x14,0x15
		 * RGB exposure times */
		{ GL843_EXPR, expr },
		{ GL843_EXPG, expg },
		{ GL843_EXPB, expb },
		/* 0x17 */
		{ GL843_TGMODE, 0 },
		{ GL843_TGW, tgw },	/* CCD TG plus width = 10 or 21 */
		/* 0x19 */
		{ GL843_EXPDMY, 42 },
		/* 0x1C */
		{ GL843_CK4MTGL, 0 },
		{ GL843_CK3MTGL, ck3mtgl },
		{ GL843_CK1MTGL, ck1mtgl },
		{ GL843_TGTIME, tgtime },
		/* 0x1D */
		{ GL843_TGSHLD, tgshld },	/* 11 or 21 */
		/* 0x9E */
		{ GL843_TGSTIME, 5 },	/*  TGSHLD * 2^TGSTIME */
		{ GL843_TGWTIME, 5 },	/*  TGW * 2^TGWTIME */
		/* 0x1E */
		{ GL843_LINESEL, linesel }, /* 0 or 1 */
		/* 0x38,0x39 */
		{ GL843_LPERIOD, lperiod },
		/* 0x52,0x53,0x54,0x55,0x56,0x57,0x58
		 * These depend on AFE clocks/pixel */
		{ GL843_RHI, rhi },
		{ GL843_RLOW, rlow },
		{ GL843_GHI, ghi },
		{ GL843_GLOW, glow },
		{ GL843_BHI, bhi },
		{ GL843_BLOW, blow },
		{ GL843_VSMP, vsmp },
		{ GL843_VSMPW, 3 },	/* Sampling pulse width */
		/* 0x70,0x71,0x72,0x73 */
		{ GL843_RSH, rsh },
		{ GL843_RSL, rsl },
		{ GL843_CPH, cph },
		{ GL843_CPL, cpl },
		/* 0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C */
		{ GL843_CK1MAP, ck1map },
		{ GL843_CK3MAP, ck3map },
		{ GL843_CK4MAP, ck4map },
	};
	CHK(write_regs(dev, frontend, ARRAY_SIZE(frontend)));

	int bwlo = 128, bwhi = 128; /* TODO: Read thresholds from img struct */

	struct regset_ent format[] = {
		/* 0x2C,0x2D,0x30,0x31,0x32,0x33 */
		{ GL843_DPISET, dpi },
		{ GL843_STRPIXEL, strpixel },
		{ GL843_ENDPIXEL, endpixel },
		/* 0x34,0x35,0x36,0x37 */
		{ GL843_DUMMY, 20 },
		{ GL843_MAXWD, maxwd },

		/* 0x04 */
		{ GL843_LINEART, fmt == PXFMT_LINEART },
		/* 0x2E,0x2F */
		{ GL843_BWHI, bwhi },
		{ GL843_BWLOW, bwlo },
		/* 0x05 */
		{ GL843_GMMENB, use_gamma },
	};
	CHK(write_regs(dev, format, ARRAY_SIZE(format)));
	ret = 0;
chk_failed:
	return ret;
}

int set_postprocessing(struct gl843_device *dev)
{
	int ret;
	int dvdset = 0, shdarea = 0, aveenb = 0;
	/* Postprocessing encompasses all pixel processing between
	 * the analog front end (AFE) and USB interface */
	struct regset_ent postprocessing[] = {
		/* 0x01 */
		{ GL843_DVDSET, dvdset },
		{ GL843_STAGGER, 0 },
		{ GL843_COMPENB, 0 },
		{ GL843_SHDAREA, shdarea },
		/* 0x03 */
		{ GL843_AVEENB, aveenb }, 	/* X scaling: 1=avg, 0=del */
		/* 0x06 */
		{ GL843_GAIN4, 0 },		/* 0/1: shading gain of 4/8. */
	};
	CHK(write_regs(dev, postprocessing, ARRAY_SIZE(postprocessing)));
	ret = 0;
chk_failed:
	return ret;
}

/* VREF settings used by the CS4400F:
 * VRHOME = {0, 1, 4, 5}
 * VRMOVE = {0}
 * VRBACK = {0, 1, 3, 4, 7}
 * VRSCAN = {0, 1, 4, 5}
 */

/* Build an acceleration speed profile for the scanner's
 * stepping motor.
 *
 * m:       Empty output object to fill with data.
 * c_start: Inital clock ticks per motor step.
 * c_end:   Final clock ticks per step. Must be less than c_start.
 *          See "speed limits" below.
 * exp:     Power function exponent. (Actually the inverse of the exponent,
 *          see the implementation note below. Larger exp => slower acceleration.)
 *          Canon uses 1.5 and 2.0.
 *
 * Speed limits for CanoScan 4400F:
 *
 * Smaller c => higher speed.
 *
 * full step:    Avoid. Does not work well at any speed.
 * half-step:    c_min = 175 (up to 1200 dpi, platen scanning)
 * quarter-step: c_min = 90 (1200 to 4800 dpi, film scanning)
 * eighth-step:  c_min = 50 (Not used)
 *
 * c_max = 13000 for all step sizes.
 *
 * The motor will tend to stall if c < c_min (running too fast),
 * and skip steps if c > c_max (running too slow).
 *
 * Implementation notes:
 *
 * The slope function is y[x] = (K/x)^(1/exp),
 * where K = c_start ^ exp, x = [1 ... MTRTBL_SIZE-1]
 * y[0] = c_start and y[x] >= c_end.
 *
 * The function generates close approximations, but not
 * exact replicas of the speed profiles used in Canon's 
 * CanoScan 4400F driver for Windows.
 *
 * Canon uses c_start = { 5617, 11234, 14298, 28597 or 24576 },
 * and exp = 1.5 or 2.0
 */
void build_accel_profile(struct motor_accel *m,
			 uint16_t c_start,
			 uint16_t c_end,
			 float exp)
{
	double K;
	int n;
	unsigned int i;

	K = pow(c_start, exp);
	m->a[0] = c_start;
	n = -1;
	for (i = 1; i < MTRTBL_SIZE; i++) {
		uint16_t c = pow(K / (double)i, 1/exp);
		if (c <= c_end) {
			m->a[i] = c_end;
			if (n < 0)
				n = i + 1;
		} else {
			m->a[i] = c;
		}
	}

	if (n < 0) {
		DBG(DBG_warn, "Cannot fit the acceleration profile into "
			"MTRTBL_SIZE steps. c_start = %d, desired c_end = %d, "
			"actual c_end = %d\n",
			c_start, c_end, m->a[MTRTBL_SIZE-1]);
		n = MTRTBL_SIZE;
	}
	/* The scanner restricts profile lengths to 1 << STEPTIM increments. */
	m->alen = ALIGN(n, 1 << STEPTIM);
	printf("m->alen = %d\n", m->alen);

	/* Get total acceleration time, used to determine Z1MOD and Z2MOD. */
	m->t_max = 0;
	for (i = 0; i < m->alen; i++)
		m->t_max += m->a[i];
}
#if 0
/* m:		Empty table to fill with data
 * step:	motor step size
 * c_max:       timer count for first motor step. That is, start speed AND
 *              start acceleration, since we're always starting from standstill.
 * c_min:       target timer count (i.e target speed).
 * vref:        voltage reference (current limit)
 *
 * Note:        Set c_max >= c_min. These parameters express speed as
 *              motor clock ticks per step, not velocity.
 */
void old_build_motor_table(struct motor_setting *m,
			enum motor_steptype type,
			uint16_t c_max,
			uint16_t c_min,
			uint8_t vref)
{
	/* Linear acceleration of a stepping motor
	 * TODO: with smooth transition from acceleration to constant speed.
	 *
	 * Reference: David Austin, Generate stepper-motor speed profiles
	 * in real time, Embedded Systems Programming, January 2005
	 */

	double c;
	unsigned int i, n;
	uint16_t *a;

	m->type = type;
	m->c_max = c_max;
	m->c_min = c_min;
	m->vref = vref;

	a = m->a;
	c = m->c_max;	/* c_max is named c0 in the article */

	/* Do two steps at lowest speed to
	 * get the motor running stably */

	a[0] = (int) (c + 0.5);
	a[1] = a[0];

	/* Build table */

	for (n = 0, i = 2; i < MTRTBL_SIZE; i++) {
		c -= 2*c / (4*i + 1.0);
		if (c <= c_min) {
			c = c_min;
			if (n == 0)
				n = i+1;
		}
		a[i] = (int) (c + 0.5);
	}
	if (n == 0) {
		DBG(DBG_warn, "Can't reach %d ticks/step\n"
			"    when starting from %d ticks/step.\n"
			"    Actual: %d\n ticks/step", c_min, c_max,
			(int) (c + 0.5));
		n = MTRTBL_SIZE;
	}

	/* Adjust table size for hardware */

	/* The GL843 multiplies stepcnt by 2^steptim
	 * If stepcnt = 255 & steptim = 2 then
	 * max table length is 255*2^2 = 1020 */

	m->alen = ALIGN(n, 1 << STEPTIM);

	/* Calculate total acceleration time (needed for Z1MOD, Z2MOD) */
	m->t_max = 0;
	for (i = 0; i < n; i++)
		m->t_max += a[i];
}


void cs4400f_build_motor_table(struct gl843_motor_setting *m,
			       unsigned int speed,
			       enum motor_step step)
{
	switch (step) {
	case FULL_STEP:
		DBG(DBG_warn, "Full steps do not work well with CS4400F. "
			" Will set fastest half step mode instead.\n");
		build_motor_table(m, HALF_STEP, 4500, 175, 0);
		break;

	case HALF_STEP:
		if (speed < 175) {
			DBG(DBG_warn, "Cannot run motor at %d ticks/half "
				"step. Reducing speed to 175.\n", speed);
			speed = 175; /* This is the scanner's highest speed */
		}
		if (speed > 13000) {
			DBG(DBG_warn, "Cannot run motor (well) at %d ticks/half "
				"step. Increasing speed to 13000.\n", speed);
			speed = 13000;
		}

		if (speed <= 700)
			build_motor_table(m, step, 4500, speed, 5);
		else if (speed <= 1000)
			build_motor_table(m, step, 8000, speed, 5);
		else if (speed <= 3500)
			build_motor_table(m, step, 15000, speed, 5);
		else if (speed <= 10000)
			build_motor_table(m, step, 33000, speed, 5);
		else
			build_motor_table(m, step, 65535, speed, 5);
		break;

	case QUARTER_STEP:
		if (speed < 90) {
			DBG(DBG_warn, "Cannot run motor at %d ticks/quarter "
				"step. Reducing speed to 90.\n", speed);
			speed = 90;
		}
		if (speed > 13000) {
			DBG(DBG_warn, "Cannot run motor (well) at %d ticks/quarter "
				"step. Increasing speed to 13000.\n", speed);
			speed = 13000;
		}

		if (speed < 500)
			build_motor_table(m, step, 2000, speed, 0);
		else if (speed <= 1000)
			build_motor_table(m, step, 3000, speed, 0);
		else if (speed <= 4500)
			build_motor_table(m, step, 10000, speed, 0);
		else if (speed <= 8000)
			build_motor_table(m, step, 33000, speed, 0);
		else if (speed <= 13000)
			build_motor_table(m, step, 44000, speed, 0);
		else
			build_motor_table(m, step, 65535, speed, 0);

		break;

	case EIGHTH_STEP:
		if (speed < 50) {
			DBG(DBG_warn, "Cannot run motor at %d ticks/quarter "
				"step. Reducing speed to 50.\n", speed);
			speed = 50;
		}

		if (speed < 100)
			build_motor_table(m, step, 1200, speed, 0);
		else if (speed < 200)
			build_motor_table(m, step, 1500, speed, 0);
		else if (speed <= 500)
			build_motor_table(m, step, 2000, speed, 0);
		else if (speed <= 1000)
			build_motor_table(m, step, 5500, speed, 0);
		else if (speed <= 2000)
			build_motor_table(m, step, 11000, speed, 0);
		else if (speed <= 3000)
			build_motor_table(m, step, 22000, speed, 0);
		else if (speed <= 4500)
			build_motor_table(m, step, 27500, speed, 0);
		else if (speed <= 7000)
			build_motor_table(m, step, 33000, speed, 0);
		else if (speed <= 10000)
			build_motor_table(m, step, 44000, speed, 0);
		else if (speed <= 15000)
			build_motor_table(m, step, 65000, speed, 0);
		else {
			/* The motor will stall on roughly 0.5% of
			 * the steps at these speeds. */
			build_motor_table(m, step, 65535, speed, 0);
		}
		break;
	}
}

void get_fastest_motor_table(struct gl843_motor_setting *m)
{
	cs4400f_build_motor_table(m, 175, HALF_STEP);
}

#if 0
	/* Command flags */

	/* 0x01 */
	{ GL843_SCAN, scan },
	/* 0x0D */
	{ GL843_JAMPCMD, 0 },		/* unused (no ADF) */
	{ GL843_DOCCMD, 0 },		/* unused (no ADF) */
	{ GL843_CCDCMD, 0 },		/* ? */
	{ GL843_FULLSTP, 0 },		/* 1 = Reset to motor full steps */
	{ GL843_SEND, 0 },		/* unused (no RS232 i/f) */
	{ GL843_CLRMCNT, clrmcnt },	/* 1 = clear FEDCNT */
	{ GL843_CLRDOCJM, 0 },		/* unused (no ADF) */
	{ GL843_CLRLNCNT, clrlncnt },	/* 1 = clear SCANCNT */
	/* 0x0E */
	{ GL843_SCANRESET, scanreset },	/* Scanner reset on write */
	/* 0x0F */
	{ GL843_MOVE, move },		/* Start motor on write */
#endif

#if 0
	struct regset_ent unknown_gpio[] = {
		{ GL843_GPIO13, gpio13 },
		{ GL843_GPIO10, gpio10 },
		{ GL843_GPOE16, gpoe16 },
		{ GL843_GPOE14, gpoe14 },
	};
	CHK(write_regs(dev, unknown_gpio, ARRAY_SIZE(unknown_gpio));
#endif
#endif

/*

Read-only registers
-------------------

0x40: GL843_DOCSNR, GL843_ADFSNR, GL843_COVERSNR, GL843_CHKVER,
      GL843_DOCJAM, GL843_HISPDFLG, GL843_MOTMFLG, GL843_DATAENB,

0x41: GL843_PWRBIT_RD, GL843_BUFEMPTY, GL843_FEEDFSH, GL843_SCANFSH,
      GL843_HOMESNR, GL843_LAMPSTS, GL843_FEBUSY, GL843_MOTORENB,

0x42,0x43,0x44: GL843_VALIDWORD,

0x48,0x49,0x4A: GL843_FEDCNT,

0x4B,0x4C,0x4D: GL843_SCANCNT,

0x4F: GL843_DOGON, GL843_ROMBSY, GL843_LCMBSY, GL843_TX232BSY,
      GL843_RX232BSY, GL843_RXREADY,

Unused IO registers
-------------------

Please update this list if you make any changes. Every device register
and IO register should be mentioned at least once in this file so that
one can be sure that no registers are ever undefined.

0x07: GL843_LAMPSIM, GL843_CCDCTL, GL843_DRAMCTL, GL843_MOVCTL,
      GL843_RAMSEL, GL843_FASTDMA, GL843_DMASEL, GL843_DMARDWR,

0x0C: GL843_SWSH, GL843_CCDLMT,

0x5C: GL843_HISPD

0x81: GL843_LOADSET,
0x82: GL843_CONTB, GL843_CONTA,
0x83: GL843_IMGSET,
0x84: GL843_PACK, GL843_PACKCNT,

0x85,0x86: undefined

0x88: GL843_RDNUM,
0x89: GL843_RS232WD,
0x8A: GL843_RS232RD,

0x8B: GL843_ROMADDR,
0x8C,0x8D: GL843_ROMWD,
0x8E,0x8F: GL843_ROMRD,

0x90,0x91: GL843_PREFED,
0x92,0x93: GL843_PSTFED,

0x95,0x96,0x97: GL843_SCANLEN,

0x9C: GL843_LCMWD,
0x9F: GL843_LCDCTL, GL843_LCMCTL, GL843_EPROMCTL, GL843_TGCTL,
      GL843_MPUCTL, GL843_MOTMPU, GL843_NEC8884, GL843_DPI9600,
0xA1: GL843_STGSET,

0xA6: GPIO24-17
0xAA: undefined

*/

/*

Film (16-bit) @ 1200 dpi:
CPH = 0,  CPL = 2,  RSH = 0,  RSL = 2

Film (16-bit) @ 2400 dpi:
CPH = 2,  CPL = 4,  RSH = 0,  RSL = 2

Film (16-bit) @ 4800 dpi:
CPH = 10, CPL = 12, RSH = 8,  RSL = 10

Platen (8-bit color) @ 50,75,100,150,200,300,400 dpi:
CPH = 0,1,     CPL = 0,3,     RSH = 0,      RSL = 0,2

Platen (8-bit color) @ 1200 dpi:
CPH = 0,1,2,   CPL = 0,3,4,   RSH = 0,      RSL = 0,2

Platen (8-bit color) @ 50 - 1200 dpi:
CPH = 1,  CPL = 3,  RSH = 0,  RSL = 2

Platen (8-bit gray or lineart) @ 300 dpi:
CPH = 2, CPL = 4 or CPH = 1, CPL = 3. TODO: Figure out why.

*/
/*
BHI = 13,17,6,
BLOW = 15,2,8,9,
GHI = 0,11,8,
GLOW = 11,14,3,
RHI = 10,11,2,5,
RLOW = 13,5,8,
*/
