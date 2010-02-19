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

#ifndef _LOW_H_
#define _LOW_H_

#include <libusb-1.0/libusb.h>
#include "regs.h"

struct gl843_device
{
	libusb_device_handle *usbdev;

	uint8_t *lbuf;		/* line buffer */
	size_t lbuf_size;	/* bytes in line buffer */
	size_t lbuf_capacity;	/* bytes allocated */

	unsigned int max_ioreg;	/* Last IO register address */
	int min_devreg;	/* Smallest devreg enum */
	int max_devreg;	/* Largest devreg enum, not counting end marker */
	int min_dirty;	/* First dirty IO register */
	int max_dirty;	/* Last dirty IO register */

	const struct regmap_ent *regmap;
	const char **devreg_names;
	const int *regmap_index;
	struct ioregister ioregs[0];	/* Shadow registers */
};

/* Constructor */
struct gl843_device *create_gl843dev(libusb_device_handle *h);

/* Destructor */
void destroy_gl843dev(struct gl843_device *dev);

/* Range checking of IO register addresses (for debugging) */
#define IOREG(addr) chk_ioreg((addr), __func__, __LINE__)
int chk_ioreg(int addr, const char *func, int line);

/* Mark register as dirty */
void mark_dirty_reg(struct gl843_device *dev, enum gl843_reg reg);

/* Fetch value in a shadow register */
unsigned int get_reg(struct gl843_device *dev, enum gl843_reg reg);

/* Store value in a shadow register */
void set_reg(struct gl843_device *dev, enum gl843_reg reg, unsigned int val);

/* Store multiple values in multiple shadow registers */
void set_regs(struct gl843_device *dev, struct regset_ent *regset, size_t len);

/* Read a set of device or IO registers from the scanner.
 *
 * This function takes a variable number of gl843_reg enums.
 * The caller must mark the end of the list with -1.
 *
 * Note: The registers are read from the scanner ordered from low to high
 * IO register address. Therefore, you should avoid touching registers that
 * will change the scanner's internal state when read, unless you know
 * what you're doing. Access such registers with read_reg() instead.
 */
int read_regs(struct gl843_device *dev, ...);

/* Read a single scanner register. */
int read_reg(struct gl843_device *dev, enum gl843_reg reg);

/* Send all dirty shadow registers to the scanner */
int flush_regs(struct gl843_device *dev);

/* Write a single scanner register */
int write_reg(struct gl843_device *dev, enum gl843_reg reg, unsigned int val);

/* Write to several scanner registers */
int write_regs(struct gl843_device *dev, struct regset_ent *regset, size_t len);

/* Write a configuration register in the analog front end (the A/D-converter) */
int write_afe(struct gl843_device *dev, int reg, int val);

/* Send motor acceleration table.
 *
 * table: table number, 1 to 5
 * tbl:   data
 * len:   number of entries (typically 1020)
 */
int send_motor_accel(struct gl843_device *dev, int table, uint16_t *tbl,
	size_t len);

/* Send gamma correction table.
 *
 * table: table number, 1 to 3 (R,G,B)
 * tbl:   data
 * len:   number of entries (typically 256)
 */
int send_gamma_table(struct gl843_device *dev, int table, uint8_t *tbl,
	size_t len);

/* Send shading correction
 *
 * buf: shading buffer
 * len: buffer size in bytes
 */
int send_shading(struct gl843_device *dev, uint16_t *buf, size_t len, int addr);

int reset_scanner(struct gl843_device *dev);

int wait_for_pixels(struct gl843_device *dev);

int start_scan(struct gl843_device *dev);

/* Set up a line buffer for read_pixels().
 * read_pixels() requests pixels from the scanner in chunks of the given size.
 * len: Buffer size in bytes.
 */
uint8_t *init_line_buffer(struct gl843_device *dev, size_t len);

/* Receive pixels from the scanner.
 * buf: destination buffer
 * len: bytes to read
 * bpp: bits per pixel
 * timeout: USB timeout in milliseconds
 */
int read_pixels(struct gl843_device *dev, uint8_t *dst, size_t len,
	unsigned int bpp, unsigned int timeout);

#endif /* _LOW_H_ */
