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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
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

#define BULK_IN		0
#define BULK_OUT	1

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
	// CS4400F specific
	dev->base_xdpi = 4800;
	dev->base_ydpi = 1200;
}

/*** USB functions. ***/

/* libusb_control_transfer wrapper that retries on interrupted system calls. */
static int usb_ctrl_xfer(libusb_device_handle *dev_handle,
			 uint8_t bmRequestType,
			 uint8_t bRequest,
			 uint16_t wValue,
			 uint16_t wIndex,
			 unsigned char *data,
			 uint16_t wLength,
			 unsigned int timeout)
{
	int i, ret;

	for (i = 0; i < 100; i++) {
		ret = libusb_control_transfer(dev_handle, bmRequestType,
			bRequest, wValue, wIndex, data, wLength, timeout);
		if (ret != LIBUSB_ERROR_INTERRUPTED)
			break;
		usleep(1000);
	}
	return ret;
}

/* libusb_bulk_transfer wrapper that retries on interrupted system calls. */
static int usb_bulk_xfer(struct libusb_device_handle *dev_handle,
			 unsigned char endpoint,
			 unsigned char *data,
			 int length,
			 int *transferred,
			 unsigned int timeout)
{
	int i, ret;

	for (i = 0; i < 100; i++) {
		ret = libusb_bulk_transfer(dev_handle, endpoint, data,
			length, transferred, timeout);
		if (ret != LIBUSB_ERROR_INTERRUPTED)
			break;
		usleep(1000);
	}
	return ret;
}

static int write_ioreg(struct gl843_device *dev, uint8_t ioreg, int val)
{
	int ret;
	uint8_t buf[2] = { ioreg, val };
	libusb_device_handle *h = dev->libusb_handle;
	const int to = 500;	/* USB timeout [ms] */

	DBG(DBG_io2, "IOREG(0x%02x) = %u (0x%02x)\n", ioreg, val, val);

	ret = usb_ctrl_xfer(h, REQ_OUT, REQ_BUF, VAL_SET_REG, 0, buf, 2, to);
	dev->ioregs[ioreg].val = val;
	dev->ioregs[ioreg].dirty = 0;
	return ret;
}

static int read_ioreg(struct gl843_device *dev, uint8_t ioreg)
{
	int ret;
	uint8_t buf[2] = { ioreg, 0 };
	libusb_device_handle *h = dev->libusb_handle;
	const int to = 500;	/* USB timeout [ms] */

	CHK(usb_ctrl_xfer(h, REQ_OUT, REQ_REG, VAL_SET_REG, 0, buf, 1, to));
	CHK(usb_ctrl_xfer(h, REQ_IN, REQ_REG, VAL_READ_REG, 0, buf, 1, to));
	dev->ioregs[ioreg].val = buf[0];
	dev->ioregs[ioreg].dirty = 0;

	DBG(DBG_io2, "IOREG(0x%02x) = %u (0x%02x)\n", ioreg, buf[0], buf[0]);
	return buf[0];
chk_failed:
	return ret;
}

static int write_bulk_setup(struct gl843_device *dev,
			    enum gl843_reg port, size_t size, int dir)
{
	int ret;
	uint8_t ioreg;
	uint8_t setup[8];
	libusb_device_handle *h = dev->libusb_handle;
	const int to = 10000;	/* USB timeout [ms] */

	ioreg = dev->regmap[dev->regmap_index[port]].ioreg;
	DBG(DBG_io2, "Writing setup packet to ioreg = %x\n", ioreg);

	setup[0] = dir; /* dir = BULK_IN or BULK_OUT */
	setup[1] = 0;	/* RAM */
	setup[2] = 0x82; /* VAL_BUF */
	setup[3] = 0;
	setup[4] = size & 0xff;
	setup[5] = (size >> 8) & 0xff;
	setup[6] = (size >> 16) & 0xff;
	setup[7] = (size >> 24) & 0xff;

	CHK(usb_ctrl_xfer(h, REQ_OUT, REQ_REG, VAL_SET_REG, 0, &ioreg, 1, to));
	CHK(usb_ctrl_xfer(h, REQ_OUT, REQ_BUF, VAL_BUF, 0, setup, 8, to));
	return 0;

chk_failed:
	return ret;
}

/* Transfer a motor acceleration table or gamma table to the scanner. */
static int send_table(struct gl843_device *dev, int table,
		      uint16_t *tbl, size_t len, int is_motortbl)
{
	int i, ret, addr;
	uint8_t *p, *buf;
	libusb_device_handle *h = dev->libusb_handle;
	const int to = 10000;	/* USB timeout [ms] */
	int size = len*2;

	addr = (table-1) * (is_motortbl ? 2048 : 256);

	buf = malloc(len * 2);
	if (!buf)
		return -ENOMEM;

	for (i = 0, p = buf; i < len; i++) {
		*p++ = tbl[i] & 0xff;
		*p++ = tbl[i] >> 8;
	}

	DBG(DBG_io, "Writing %zu entries to %s table %d.\n", len,
		is_motortbl ? "motor" : "gamma", table);

	set_reg(dev, GL843_MTRTBL, is_motortbl);
	set_reg(dev, GL843_GMMADDR, addr);
	CHK(flush_regs(dev));
	CHK(write_bulk_setup(dev, GL843__GMMWRDATA_, size, BULK_OUT));
	CHK(usb_bulk_xfer(h, 2, buf, size, &size, to));
	set_reg(dev, GL843_MTRTBL, 0);
	set_reg(dev, GL843_GMMADDR, 0);
	CHK(flush_regs(dev));
	free(buf);

	return 0;

chk_failed:
	free(buf);
	DBG(DBG_error, "libusb error: %s\n", sanei_libusb_strerror(ret));
	return -EIO;
}

int send_motor_table(struct gl843_device *dev,
		     int table,
		     size_t len,
		     uint16_t *a)
{
	return send_table(dev, table, a, len, 1);
}

int send_gamma_table(struct gl843_device *dev,
		     int table,
		     size_t len,
		     uint16_t *g)
{
	return send_table(dev, table, g, len, 0);
}

int send_shading(struct gl843_device *dev, uint8_t *buf, size_t size, int addr)
{
	int ret;
	libusb_device_handle *h = dev->libusb_handle;
	const int to = 10000;	/* USB timeout [ms] */
	int outsize;

	CHK(write_reg(dev, GL843_RAMADDR, addr));
	CHK(write_bulk_setup(dev, GL843__RAMWRDATA_, size, BULK_OUT));
	CHK(usb_bulk_xfer(h, 2, buf, size, &outsize, to));
	return 0;
chk_failed:
	DBG(DBG_error, "libusb error: %s\n", sanei_libusb_strerror(ret));
	return -EIO;
}

int recv_image(struct gl843_device *dev, uint8_t *buf, size_t size, int addr)
{
	int ret;
	int ep, port, len;
	libusb_device_handle *h = dev->libusb_handle;
	const int to = 10000;	/* USB timeout [ms] */

	DBG(DBG_io, "buf = %p, addr = 0x%x, size = %zu\n", buf, addr, size);

	write_reg(dev, GL843_RAMADDR, addr);
	CHK(write_bulk_setup(dev, GL843__RAMRDDATA_, size, BULK_IN));

	/* Transfer bulk data */

	int total = 0;
	while (size > 0) {
		int outlen = 0;
		len = size < 16384 ? size : 16384;
		ret = usb_bulk_xfer(h, 0x81, buf, len, &outlen, to);
		total += outlen;
		DBG(DBG_io, "requested %d, received %d bytes. (%d total)\n",
			len, outlen, total);
		if (ret == LIBUSB_ERROR_OVERFLOW && len > outlen) {
			DBG(DBG_io2, "overflow detected. len = %d > outlen = %d\n",
				len, outlen);
			/* Ignore underflows for now. FIXME. */
		} else if (ret < 0)
			goto chk_failed;
		size -= outlen;
		buf += outlen;
		if (outlen < len)
			break;
	}
	return 0;
chk_failed:
	DBG(DBG_error, "libusb error: %s\n", sanei_libusb_strerror(ret));
	return -EIO;
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

	for (i = dev->min_dirty; i <= dev->max_dirty; i++) {
		if (dev->ioregs[i].dirty != 0) {
			int ret = write_ioreg(dev, i, dev->ioregs[i].val);
			if (ret < 0)
				return ret;
		}
	}
	dev->min_dirty = dev->max_ioreg + 1;
	dev->max_dirty = 0;
	return 0;
}

void mark_devreg_dirty(struct gl843_device *dev, enum gl843_reg reg)
{
	const struct regmap_ent *rmap;
	if (reg > dev->max_devreg)
		return;
	rmap = dev->regmap + dev->regmap_index[reg];
	for (; rmap->devreg == reg; ++rmap) {
		mark_ioreg_dirty(dev, rmap->ioreg, 0xff);
	}
}

int write_reg(struct gl843_device *dev, enum gl843_reg reg, unsigned int val)
{
	set_reg(dev, reg, val);
	return flush_regs(dev);
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
	int reg = 0;
	va_list ap;

	va_start(ap, dev);
	/* Mark the listed registers as dirty. */
	while ((reg = va_arg(ap, int)) >= 0) {
		mark_devreg_dirty(dev, reg);
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

/* Read a single scanner register.
 * Returns the register value (>= 0) on success, or < 0 on failure.
 */
int read_reg(struct gl843_device *dev, enum gl843_reg reg)
{
	int ret;
	ret = read_regs(dev, reg);
	if (ret < 0)
		return ret;
	return get_reg(dev, reg);
}
#if 0
void diff_regs(struct gl843_device *dev)
{
	enum gl843_reg i;
	int oldval, val;
	for (i = GL843_MIN_IOREG; i <= GL843_MAX_IOREG; i++) {
		oldval = dev->ioregs[i].val;
		val = read_ioreg(dev, i);
		if (val != oldval) {
			printf("IOREG(0x%x): cached = %d, in scanner = %d\n",
				i, oldval, val);
		}
	}
}
#endif
/* Write a configuration register in the analog front end (the A/D-converter) */
int write_afe(struct gl843_device *dev, int reg, int val)
{
	int ret;
	int fe_busy = 1;
	int timeout = 10;	/* Arbitrary choice */

	DBG(DBG_io, "reg = 0x%x, value = 0x%x (%d)\n", reg, val, val);

	while (fe_busy && timeout) {
		fe_busy = read_reg(dev, GL843_FEBUSY);
		if (fe_busy < 0)
			goto chk_failed;
		timeout--;
	}
	if (timeout == 0) {
		DBG(DBG_error, "Cannot write config register %d in the "
			"analog frontend (AFE): The AFE is busy.\n", reg);
		return -EBUSY;
	}

	CHK(write_reg(dev, GL843_FEWRA, reg));
	CHK(write_reg(dev, GL843_FEWRDATA, val));
	return 0;
chk_failed:
	return -EIO;
}
