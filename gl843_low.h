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

#include <libusb-1.0/libusb.h>
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

	unsigned int base_xdpi;/* Dots per inch in CCD */
	unsigned int base_ydpi;/* Dots per inch at motor full step. */
};

/* xfer_bulk() and xfer_table() flags */

/* Range checking of IO register addresses (for debugging) */
#define IOREG(addr) chk_ioreg((addr), __func__, __LINE__)

int chk_ioreg(int addr, const char *func, int line);
void create_device(struct gl843_device *dev);
int xfer_bulk(struct gl843_device *dev, uint8_t *buf, size_t size,
	int addr, int flags);
int send_motor_table(struct gl843_device *dev, int table, size_t len, uint16_t *a);
int send_gamma_table(struct gl843_device *dev, int table, size_t len, uint16_t *g);
int send_shading(struct gl843_device *dev, uint8_t *buf, size_t size, int addr);
int recv_image(struct gl843_device *dev, uint8_t *buf, size_t size, int addr);

void set_reg(struct gl843_device *dev, enum gl843_reg reg, unsigned int val);
void set_regs(struct gl843_device *dev, struct regset_ent *regset, size_t len);
unsigned int get_reg(struct gl843_device *dev, enum gl843_reg reg);
int flush_regs(struct gl843_device *dev);
int write_reg(struct gl843_device *dev, enum gl843_reg reg, unsigned int val);
int read_regs(struct gl843_device *dev, ...);
int read_reg(struct gl843_device *dev, enum gl843_reg reg);
//void diff_regs(struct gl843_device *dev);
int write_afe(struct gl843_device *dev, int reg, int val);

#endif /* _GL843_LOW_H_ */
