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

#include <string.h>
#include <stdarg.h>
#include <libusb-1.0/libusb.h>

#define GL843_PRIVATE

#include "gl843_low.h"
#include "gl843_util.h"

#define REQ_IN		0xc0
#define REQ_OUT		0x40
#define REQ_REG		0x0c
#define REQ_BUF		0x04
#define VAL_BUF		0x82
#define VAL_SET_REG	0x83
#define VAL_READ_REG	0x84

static struct ioregister gl843_ioregs[GL843_MAX_IOREG+1];

int chk_ioreg(int addr, const char *func, int line)
{
	if (addr < 0 || addr > GL843_MAX_IOREG) {
		vprintf_dbg(0, func, line, "Internal error: register "
			"address 0x%x is out of range.\n", addr);
	}
	return addr;
}

/* Constructor */
void create_device(struct gl843_device *dev)
{
	int i;

	dev->ioregs = gl843_ioregs;
	dev->regmap = gl843_regmap;
	dev->devreg_names = gl843_devreg_names;
	dev->regmap_index = gl843_regmap_index;
	dev->max_ioreg = GL843_MAX_IOREG;
	dev->min_devreg = dev->max_ioreg + 1;
	dev->max_devreg = GL843_MAX_DEVREG - 1;
	dev->max_dirty = -1;
	dev->min_dirty = dev->max_ioreg;

	for (i = 0; i < GL843_MAX_IOREG; i++) {
		memset(&dev->ioregs[i], 0, sizeof(dev->ioregs[0]));
		dev->ioregs[i].ioreg = i;
	}
}

/*** USB functions. TODO: Use SANE's USB API. ***/

/* Write to an I/O register from the scanner.
 *
 * ioreg: register address, 0x00 - dev->max_ioreg
 * val:   8-bit unsigned value
 * Returns: register value, >= 0 on success, or < 0 on failure.
 */
int write_ioreg(struct gl843_device *dev, uint8_t ioreg, int val)
{
	int ret;
	uint8_t buf[2] = { ioreg, val };
	libusb_device_handle *h = dev->libusb_handle;
	const int to = 500;	/* USB timeout [ms] */

	DBG(DBG_io2, "reg = 0x%02x, val = 0x%02x\n", ioreg, val);

	if (ioreg > dev->max_ioreg)
		goto bad_regnum;
	ret = libusb_control_transfer(h, REQ_OUT, REQ_BUF, VAL_SET_REG, 0,
		buf, 2, to);
	if (ret < 0)
		goto usb_error;

	dev->ioregs[ioreg].val = val;
	dev->ioregs[ioreg].dirty = 0;
	return 0;

bad_regnum:
	DBG(DBG_error, "bad IO register 0x%02x\n", ioreg);
	return -1;
usb_error:
	DBG(DBG_error, "libusb error: %s\n", sanei_libusb_strerror(ret));
	return -1;
}

/* Read an I/O register from the scanner.
 *
 * ioreg: register address
 * Returns: register value, register value >= 0 on success, or < 0 on failure.
 */
int read_ioreg(struct gl843_device *dev, uint8_t ioreg)
{
	int ret;
	uint8_t buf[2] = { ioreg, 0 };
	libusb_device_handle *h = dev->libusb_handle;
	const int to = 500;	/* USB timeout [ms] */

	DBG(DBG_io2, "reg = 0x%02x\n", ioreg);

	if (ioreg > dev->max_ioreg)
		goto bad_regnum;

	ret = libusb_control_transfer(h, REQ_OUT, REQ_REG, VAL_SET_REG,
		0, buf, 1, to);
	if (ret < 0)
		goto usb_error;
	ret = libusb_control_transfer(h, REQ_IN, REQ_REG, VAL_READ_REG,
		0, buf, 1, to);
	if (ret < 0)
		goto usb_error;

	dev->ioregs[ioreg].val = buf[0];
	dev->ioregs[ioreg].dirty = 0;

	DBG(DBG_io2, "val = 0x%02x\n", buf[0]);

	return buf[0];

bad_regnum:
	DBG(DBG_error, "bad IO register 0x%02x\n", ioreg);
	return -1;
usb_error:
	DBG(DBG_error, "libusb error: %s\n", sanei_libusb_strerror(ret));
	return -1;
}

/* Perform a USB bulk transfer to/from the scanner.
 *
 * buf:  data buffer
 * size: number of bytes to transfer
 * addr: address in scanner RAM
 * flags: Set direction and memory type by bitwise or'ing of one of
 *        the direction flags and one of the memory area flags, below.
 *        Direction flags:   BULK_IN or BULK_OUT
 *	  Memory area flags: GAMMA_SRAM or MOTOR_SRAM or IMG_DRAM
 *
 * Returns: 0 on success, < 0 on failure.
 */
int xfer_bulk(struct gl843_device *dev, uint8_t *buf, size_t size, int addr, int flags)
{
	int ret;
	int dir = flags & (BULK_IN | BULK_OUT);
	int ep, port, len;
	uint8_t ioreg;
	uint8_t setup[8];
	libusb_device_handle *h = dev->libusb_handle;
	const int to = 10000;	/* USB timeout [ms] */

	switch (flags & (GAMMA_SRAM | MOTOR_SRAM | IMG_DRAM)) {
	case IMG_DRAM:
		port = (dir == BULK_IN) ? GL843__RAMRDDATA_ : GL843__RAMWRDATA_;
		set_reg(dev, GL843_RAMADDR, addr);
		break;
	case GAMMA_SRAM:
		port = (dir == BULK_IN) ? GL843__GMMRDDATA_ : GL843__GMMWRDATA_;
		set_reg(dev, GL843_MTRTBL, 0);
		set_reg(dev, GL843_GMMADDR, addr);
		break;
	case MOTOR_SRAM:
		port = (dir == BULK_IN) ? GL843__GMMRDDATA_ : GL843__GMMWRDATA_;
		set_reg(dev, GL843_MTRTBL, 1);
		set_reg(dev, GL843_GMMADDR, addr);
		break;
	default:
		DBG(DBG_io, "invalid flags = %d\n", flags);
		return -1;
	}
	if (flush_regs(dev) < 0)
		return -1;

	DBG(DBG_io, "%s addr = 0x%x, size = %zu, flags = %d\n",
		(dir == BULK_IN) ? "IN" : "OUT", addr, size, flags);

	/* Send setup */

	ioreg = dev->regmap[dev->regmap_index[port]].ioreg;
	DBG(DBG_io2, "Writing setup packet to ioreg = %x\n", ioreg);

	setup[0] = dir;
	setup[1] = 0;	/* RAM */
	setup[2] = VAL_BUF & 0xff;
	setup[3] = (VAL_BUF >> 8) & 0xff;
	setup[4] = size & 0xff;
	setup[5] = (size >> 8) & 0xff;
	setup[6] = (size >> 16) & 0xff;
	setup[7] = (size >> 24) & 0xff;

	ret = libusb_control_transfer(h, REQ_OUT, REQ_REG, VAL_SET_REG,
		0, &ioreg, 1, to);
	if (ret < 0)
		goto usb_error;
	ret = libusb_control_transfer(h, REQ_OUT, REQ_BUF, VAL_BUF,
		0, setup, sizeof(setup), to);
	if (ret < 0)
		goto usb_error;

	/* Transfer bulk data */

	ep = (dir == BULK_OUT) ? 2 : 0x81;

	int total = 0;
	while (size > 0) {
		/* The Windows driver for Canoscan 4400F requests
		 * 16KB chunks, so we do the same. */
		len = (size > 16384) ? 16384 : size;
		DBG(DBG_io2, "transferring %d bytes....", len);
		ret = libusb_bulk_transfer(h, ep, buf, len, &len, to);
		total += len;
		DBG(DBG_io2, "or actually %d bytes. (%d total)\n", len, total);
		if (ret < 0)
			goto usb_error;
		size -= len;
		buf += len;
	}
	return 0;
usb_error:
	DBG(DBG_error, "libusb error: %s\n", sanei_libusb_strerror(ret));
	return -1;
}

/*** Device register access functions. ***/

/* Set dirty bits in an IO register */
static void mark_ioreg_dirty(struct gl843_device *dev, int ioreg, int mask)
{
	(dev->ioregs + ioreg)->dirty |= mask;
	if (dev->min_dirty > ioreg)
		dev->min_dirty = ioreg;
	if (dev->max_dirty < ioreg)
		dev->max_dirty = ioreg;
}

/* Store value in a device register (by name) or IO register (by address)
 *
 * reg: register name (enum gl843_devreg) or address (IOREG(address))
 * val: register value
 *
 * Call flush_regs() to actually update the register in the scanner.
 */
void set_reg(struct gl843_device *dev, enum gl843_reg reg, unsigned int val)
{
	const struct regmap_ent *rmap;

	if (reg < 0 || reg > dev->max_devreg)
		return;

	if (reg <= dev->max_ioreg)
		DBG(DBG_io, "IOREG(0x%x) = %u (0x%x)\n", reg, val, val);
	else
		DBG(DBG_io, "%s = %u (0x%x)\n",
			dev->devreg_names[reg - dev->min_devreg], val, val);

	rmap = dev->regmap + dev->regmap_index[reg];
	for (; rmap->devreg == reg; ++rmap) {
		int shift = rmap->shift;
		int mask = rmap->mask;
		struct ioregister *ioreg = dev->ioregs + rmap->ioreg;

		ioreg->val &= ~mask;
		ioreg->val |= (shift >= 0)
			? (val <<  shift) & mask
			: (val >> -shift) & mask;
		mark_ioreg_dirty(dev, rmap->ioreg, mask);
	}
}

/* Set several device or IO registers. */
void set_regs(struct gl843_device *dev, struct regset_ent *regset, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++, regset++) {
		set_reg(dev, regset->reg, regset->val);
	}
}

/* Fetch value in a device register (by name) or IO register (by address)
 *
 * This function fetches cached register values.
 * Call read_devregs() first to actually read from the scanner.
 */
unsigned int get_reg(struct gl843_device *dev, enum gl843_reg reg)
{
	const struct regmap_ent *rmap;
	unsigned int val = 0;

	if (reg < 0 || reg > dev->max_devreg)
		return -1;

	rmap = dev->regmap + dev->regmap_index[reg];
	for (; rmap->devreg == reg; ++rmap) {
		int shift = rmap->shift;
		int mask = rmap->mask;
		struct ioregister *ioreg = dev->ioregs + rmap->ioreg;

		ioreg = dev->ioregs + rmap->ioreg;
		val |= (shift >= 0)
			? (ioreg->val & mask) >> shift
			: (ioreg->val & mask) << -shift;
	}
	return val;
}

/* Write dirty registers to the scanner */
int flush_regs(struct gl843_device *dev)
{
	int i;
	int ret;

	for (i = dev->min_dirty; i <= dev->max_dirty; i++) {
		if (dev->ioregs[i].dirty != 0) {
			ret = write_ioreg(dev, i, dev->ioregs[i].val);
			if (ret < 0)
				goto usb_error;
		}
	}
	dev->min_dirty = dev->max_ioreg + 1;
	dev->max_dirty = 0;
	return 0;
usb_error:
	/* Don't print an error message; write_ioreg() already did. */
	return ret;
}

/* Read a set of device or IO registers from the scanner.
 *
 * This function takes a variable number of gl843_reg enums.
 * The caller must mark the end of the list with -1.
 *
 * Note: The registers are read from the scanner ordered from low to high
 * IO register address. Therefore, you should avoid touching registers that
 * will change the scanner's internal state when read, unless you know
 * what you're doing. Access such registers with read_ioreg() instead.
 */
int read_regs(struct gl843_device *dev, ...)
{
	int ret;
	int reg;
	va_list ap;
	va_start(ap, dev);

	/* Find the IO registers we need to read, and mark them as dirty. */
	while ((reg = va_arg(ap, int)) >= 0) {
		const struct regmap_ent *rmap;
		if (reg > dev->max_devreg)
			continue;
		rmap = dev->regmap + dev->regmap_index[reg];
		for (; rmap->devreg == reg; ++rmap) {
			mark_ioreg_dirty(dev, rmap->ioreg, 0xff);
		}
	}
	va_end(ap);

	/* Read dirty IO registers, and mark them as clean. */
	for (reg = dev->min_dirty; reg <= dev->max_dirty; reg++) {
		if (dev->ioregs[reg].dirty == 0)
			continue;
		ret = read_ioreg(dev, reg);
		if (ret < 0)
			goto usb_error;
	}
	dev->min_dirty = dev->max_ioreg + 1;
	dev->max_dirty = 0;
	return 0;
usb_error:
	/* Don't print an error message. read_ioreg() already did. */
	return ret;
}

/* Write a configuration register in the analog front end (the A/D-converter) */
int write_afe(struct gl843_device *dev, int reg, int val)
{
	int fe_busy = 1;
	int timeout = 10;	/* 10 <==> 40 ms as one register read uses
				 * 2 USB requests to and 2 responses, 1 ms each */
	while (fe_busy && timeout) {
		if (read_regs(dev, GL843_FEBUSY, -1) < 0);
			return -1; /* USB error. */
		fe_busy = get_reg(dev, GL843_FEBUSY);
		timeout--;
	}
	if (timeout == 0) {
		DBG(DBG_error, "Cannot write config register %d in the "
			"analog frontend (AFE): The AFE is busy.\n", reg);
		return -1;
	}

	set_reg(dev, GL843_FEWRA, reg);
	set_reg(dev, GL843_FEWRDATA, val);
	if (flush_regs(dev) < 0)
		return -1; /* USB error. */
	return 0;
}
