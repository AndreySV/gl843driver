/* Low-level I/O functions for GL483-based scanners.
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

#ifndef _GL843_LOW_H_
#define _GL843_LOW_H_

#include "gl843_regs.h"

struct gl843_device
{
	libusb_device_handle *libusb_handle;
	struct ioregister *ioregs;
	const struct regmap_ent *regmap;
	const char **devreg_names;
	const int *regmap_index;
	int max_ioreg;	/* Last IO register address */
	int min_devreg;	/* Smallest devreg enum */
	int max_devreg;	/* Largest devreg enum, not counting end marker */
	int min_dirty;	/* First dirty IO register */
	int max_dirty;	/* Last dirty IO register */
};

/* xfer_bulk() flags */
#define BULK_IN		0
#define BULK_OUT	1
#define IMG_DRAM	0
#define GAMMA_SRAM 	2
#define MOTOR_SRAM	4

/* Range checking of IO register addresses (for debugging) */
#define IOREG(addr) chk_ioreg((addr), __func__, __LINE__)

int chk_ioreg(int addr, const char *func, int line);
void create_device(struct gl843_device *dev);
int write_ioreg(struct gl843_device *dev, uint8_t ioreg, int val);
int read_ioreg(struct gl843_device *dev, uint8_t ioreg);
int xfer_bulk(struct gl843_device *dev, uint8_t *buf, size_t size,
	int addr, int flags);
void set_reg(struct gl843_device *dev, enum gl843_reg reg, unsigned int val);
void set_regs(struct gl843_device *dev, struct regset_ent *regset, size_t len);
unsigned int get_reg(struct gl843_device *dev, enum gl843_reg reg);
int write_dirty_regs(struct gl843_device *dev);
#if 0
int read_regs(struct gl843_device *dev, ...);
#endif

#endif /* _GL843_LOW_H_ */
