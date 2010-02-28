/*
 * Copyright (C) 2009-2010 Andreas Robinson <andr345 at gmail dot com>
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>
#include <sane/sane.h>

#define GL843_PRIVATE
#undef CHK_DEBUG

#include "low.h"
#include "util.h"

#define REQ_IN		0xc0
#define REQ_OUT		0x40
#define REQ_REG		0x0c
#define REQ_BUF		0x04
#define VAL_BUF		0x82
#define VAL_SET_REG	0x83
#define VAL_READ_REG	0x84

#define BULK_IN		0
#define BULK_OUT	1

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
	}
	return ret;
}

/* Construct gl843 device and register cache */
struct gl843_device *create_gl843dev(libusb_device_handle *h)
{
	int i;
	struct gl843_device *dev;

	dev = calloc(sizeof(*dev) +
		sizeof(dev->ioregs[0]) * (GL843_MAX_IOREG + 1), 1);
	if (!dev)
		return NULL;

	dev->usbdev = h;

	dev->lbuf = NULL;
	dev->lbuf_size = 0;
	dev->lbuf_capacity = 0;

	dev->pconv = NULL;

	dev->regmap = gl843_regmap;
	dev->devreg_names = gl843_devreg_names;
	dev->regmap_index = gl843_regmap_index;
	dev->max_ioreg = GL843_MAX_IOREG;
	dev->min_devreg = dev->max_ioreg + 1;
	dev->max_devreg = GL843_MAX_DEVREG - 1;
	dev->max_dirty = -1;
	dev->min_dirty = dev->max_ioreg;

	for (i = 0; i < GL843_MAX_IOREG; i++)
		dev->ioregs[i].ioreg = i;

	return dev;
}

/* Destructor */
void destroy_gl843dev(struct gl843_device *dev)
{
	if (dev) {
		free(dev->lbuf);
		dev->lbuf = NULL;
		dev->lbuf_capacity = 0;
	}
	free(dev);
}

static int chk_reg(int addr, int max_addr, const char *func, int line)
{
	if (addr < 0 || addr > max_addr) {
		vprintf_dbg(0, func, line, "Internal error: register "
			"address 0x%x (%d) is out of range, max is 0x%x (%d).\n",
			addr, addr, max_addr, max_addr);
		return 0;
	}
	return 1;
}

int chk_ioreg(int addr, const char *func, int line)
{
	if (!chk_reg(addr, GL843_MAX_IOREG, func, line))
		return 0;
	else
		return addr;
}

static void mark_ioreg_dirty(struct gl843_device *dev, int ioreg, int mask)
{
	(dev->ioregs + ioreg)->dirty |= mask;
	if (dev->min_dirty > ioreg)
		dev->min_dirty = ioreg;
	if (dev->max_dirty < ioreg)
		dev->max_dirty = ioreg;
}

void mark_dirty_reg(struct gl843_device *dev, enum gl843_reg reg)
{
	const struct regmap_ent *rmap;
	if (!chk_reg(reg, dev->max_devreg, __func__, 0))
		return;
	rmap = dev->regmap + dev->regmap_index[reg];
	for (; rmap->devreg == reg; ++rmap)
		mark_ioreg_dirty(dev, rmap->ioreg, rmap->mask);
}

/* Read a value from the register cache */
unsigned int get_reg(struct gl843_device *dev, enum gl843_reg reg)
{
	const struct regmap_ent *rmap;
	unsigned int val = 0;

	if (!chk_reg(reg, dev->max_devreg, __func__, 0))
		return 0;
#if 0
	if (reg <= dev->max_ioreg)
		DBG(DBG_io, "IOREG(0x%x) = %u (0x%x)\n", reg, val, val);
	else
		DBG(DBG_io, "%s = %u (0x%x)\n",
			dev->devreg_names[reg - dev->min_devreg], val, val);
#endif
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

/* Write value to the register cache */
void set_reg(struct gl843_device *dev, enum gl843_reg reg, unsigned int val)
{
	const struct regmap_ent *rmap;

	if (!chk_reg(reg, dev->max_devreg, __func__, 0))
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

/* Write multiple values to the register cache */
void set_regs(struct gl843_device *dev, struct regset_ent *regset, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++, regset++) {
		set_reg(dev, regset->reg, regset->val);
	}
}

/* Read an IO-register from the scanner.
 * See regs.h
 */
static int read_ioreg(struct gl843_device *dev, uint8_t ioreg)
{
	int ret;
	uint8_t buf[2] = { ioreg, 0 };
	libusb_device_handle *h = dev->usbdev;
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

/* Write to an IO-register in the scanner.
 * See regs.h
 */
static int write_ioreg(struct gl843_device *dev, uint8_t ioreg, int val)
{
	int ret;
	uint8_t buf[2] = { ioreg, val };
	libusb_device_handle *h = dev->usbdev;
	const int to = 500;	/* USB timeout [ms] */

	DBG(DBG_io2, "IOREG(0x%02x) = %u (0x%02x)\n", ioreg, val, val);

	ret = usb_ctrl_xfer(h, REQ_OUT, REQ_BUF, VAL_SET_REG, 0, buf, 2, to);
	dev->ioregs[ioreg].val = val;
	dev->ioregs[ioreg].dirty = 0;
	return ret;
}

/* Read, and cache, multiple scanner registers */
int read_regs(struct gl843_device *dev, ...)
{
	int ret;
	int reg = 0;
	va_list ap;

	va_start(ap, dev);
	/* Mark the listed registers as dirty. */
	while ((reg = va_arg(ap, int)) >= 0) {
		mark_dirty_reg(dev, reg);
	}
	va_end(ap);

	/* Read dirty IO registers, and mark them as clean. */
	for (reg = dev->min_dirty; reg <= dev->max_dirty; reg++) {
		if (dev->ioregs[reg].dirty == 0)
			continue;
		CHK(read_ioreg(dev, reg));
	}
	dev->min_dirty = dev->max_ioreg + 1;
	dev->max_dirty = 0;
	return 0;
chk_failed:
	return ret;
}

/* Read, and cache, a scanner register */
int read_reg(struct gl843_device *dev, enum gl843_reg reg)
{
	int ret;
	CHK(read_regs(dev, reg, -1));
	return get_reg(dev, reg);
chk_failed:
	return ret;
}

/* Send dirty registers in the cache to the scanner */
int flush_regs(struct gl843_device *dev)
{
	int i, ret;

	for (i = dev->min_dirty; i <= dev->max_dirty; i++) {
		if (dev->ioregs[i].dirty != 0) {
			CHK(write_ioreg(dev, i, dev->ioregs[i].val));
		}
	}
	dev->min_dirty = dev->max_ioreg + 1;
	dev->max_dirty = 0;
	return 0;
chk_failed:
	return ret;
}

/* Write to, and cache, a scanner register */
int write_reg(struct gl843_device *dev, enum gl843_reg reg, unsigned int val)
{
	set_reg(dev, reg, val);
	return flush_regs(dev);
}

/* Write to, and cache, multiple scanner registers */
int write_regs(struct gl843_device *dev, struct regset_ent *regset, size_t len)
{
	set_regs(dev, regset, len);
	return flush_regs(dev);
}

/* Prepare the scanner for a bulk transfer */
static int write_bulk_setup(struct gl843_device *dev,
			    enum gl843_reg port, size_t size, int dir)
{
	int ret;
	uint8_t ioreg;
	uint8_t setup[8];
	libusb_device_handle *h = dev->usbdev;
	const int to = 1000;	/* USB timeout [ms] */

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

/* Write a register in the analog frontend (AFE), aka AD-converter.
 * reg: register address
 * val: register value (8-bit)
 */
int write_afe(struct gl843_device *dev, int reg, int val)
{
	int ret;
	int fe_busy = 1;
	int timeout = 10;	/* Arbitrary choice */

	DBG(DBG_io, "reg = 0x%x, value = 0x%x (%d)\n", reg, val, val);

	while (fe_busy && timeout) {
		fe_busy = read_reg(dev, GL843_FEBUSY);
		CHK(fe_busy);
		timeout--;
	}
	if (timeout == 0) {
		DBG(DBG_error, "Cannot write config register %d in the "
			"analog frontend (AFE): The AFE is busy.\n", reg);
		return LIBUSB_ERROR_BUSY;
	}

	CHK(write_reg(dev, GL843_FEWRA, reg));
	CHK(write_reg(dev, GL843_FEWRDATA, val));
chk_failed:
	return ret;
}

/* Send a stepping motor acceleration table to the scanner
 * table: table number, 1 - 5.
 * tbl: acceleration table buffer
 * len: buffer length in bytes
 */
int send_motor_accel(struct gl843_device *dev,
		     int table, uint16_t *tbl, size_t len)
{
	int ret, outlen;

	DBG(DBG_io, "sending motor table %d, (%zu entries)\n", table, len);

	if (host_is_big_endian())
		swap_buffer_endianness(tbl, tbl, len);

	set_reg(dev, GL843_MTRTBL, 1);
	set_reg(dev, GL843_GMMADDR, (table-1) * 2048);
	CHK(flush_regs(dev));
	CHK(write_bulk_setup(dev, GL843__GMMWRDATA_, len*2, BULK_OUT));
	CHK(usb_bulk_xfer(dev->usbdev, 2,
		(uint8_t *) tbl, len*2, &outlen, 1000));
	set_reg(dev, GL843_MTRTBL, 0);
	set_reg(dev, GL843_GMMADDR, 0);
	CHK(flush_regs(dev));
chk_failed:
	/* Restore endianness in buffer */
	if (host_is_big_endian())
		swap_buffer_endianness(tbl, tbl, len);
	return ret;
}

/* Send a gamma correction table to the scanner.
 * table: table number, 1, 2 or 3. (One for each color component)
 * tbl: gamma table buffer
 * len: buffer length in bytes
 */
int send_gamma_table(struct gl843_device *dev,
		     int table, uint8_t *tbl, size_t len)
{
	int ret, outlen;

	DBG(DBG_io, "sending gamma table %d, (%zu entries)\n", table, len);

	set_reg(dev, GL843_MTRTBL, 1);
	set_reg(dev, GL843_GMMADDR, (table-1) * 256);
	CHK(flush_regs(dev));
	CHK(write_bulk_setup(dev, GL843__GMMWRDATA_, len, BULK_OUT));
	CHK(usb_bulk_xfer(dev->usbdev, 2, tbl, len, &outlen, 1000));
	set_reg(dev, GL843_MTRTBL, 0);
	set_reg(dev, GL843_GMMADDR, 0);
	CHK(flush_regs(dev));
chk_failed:
	return ret;
}

/* Send shading data to the scanner.
 * buf: shading data buffer
 * len: length in bytes
 */
int send_shading(struct gl843_device *dev, uint16_t *buf, size_t len, int addr)
{
	const int BLKSIZE = 512;
	int ret, n, outlen;
	uint8_t p[BLKSIZE];

	/* Send 42 pixels (42 * 12 = 504 bytes) at a time, padded to 512 bytes.
	 * The scanner ignores the last 8 bytes in every 512 byte block,
	 * probably because they are less than a full pixel.
	 */
	n = len + (len / 504 + 1) * 8; /* Actual number of bytes to send */

	CHK(write_reg(dev, GL843_RAMADDR, addr));
	CHK(write_bulk_setup(dev, GL843__RAMWRDATA_, n, BULK_OUT));

	DBG(DBG_io, "sending %zu + %zu bytes data + padding.\n", len, n - len);

	for (; n > 0; n -= BLKSIZE) {
		memcpy(p, buf, (len >= BLKSIZE) ? BLKSIZE : len);
		CHK(usb_bulk_xfer(dev->usbdev, 2,
			p, 512, &outlen, 10000));
		buf += 504/2;
		len -= 504;
	}
	ret = 0;
chk_failed:
	return ret;
}

/* TODO: add timeout parameter */
int wait_for_pixels(struct gl843_device *dev)
{
	int ret = 1;
	while (ret > 0) {
		ret = read_reg(dev, GL843_BUFEMPTY);
		usleep(1000);
	}
	return ret;
}

/* Receive pixels from the scanner.
 * buf: destination buffer
 * len: bytes to read.
 *	Must equal a full number of pixels.
 *      The GL843 doesn't handle arbitrary lengths either,
 *      however reading complete lines of pixels works well.
 * bpp: bits per pixel
 * timeout: USB timeout in milliseconds
 */
static int recv_pixels(struct gl843_device *dev,
		       uint8_t *buf,
		       size_t len,
		       unsigned int bpp,
		       unsigned int timeout)
{
	int ret, outlen;

	CHK(write_reg(dev, GL843_RAMADDR, 0));
	CHK(write_bulk_setup(dev, GL843__RAMRDDATA_, len, BULK_IN));
	CHK(usb_bulk_xfer(dev->usbdev, 0x81, buf, len, &outlen, timeout));
	DBG(DBG_io, "requesting %zu bytes, got %d.\n", len, outlen);

	if (dev->pconv) {
		int n = 8*outlen / bpp;
		if (outlen % (bpp / 8)) {
			DBG(DBG_warn, "Warning: outlen is not a full number of pixels\n");
		}
		n = dev->pconv->convert(dev->pconv, buf, n);
		outlen = n * bpp / 8;
	}
	ret = outlen;
chk_failed:
	return ret;
}

int reset_scanner(struct gl843_device *dev)
{
	return write_reg(dev, GL843_SCANRESET, 1);
}

int start_scan(struct gl843_device *dev)
{
	int ret;

	set_reg(dev, GL843_MTRPWR, 1);
	set_reg(dev, GL843_SCAN, 1);
	CHK(flush_regs(dev));
	CHK(write_reg(dev, GL843_MOVE, 16));

	ret = 0;
chk_failed:
	return ret;
}

/* Set up a line buffer for read_pixels().
 * read_pixels() requests pixels from the scanner in chunks of the given size.
 * len: Buffer size in bytes.
 */
uint8_t *init_line_buffer(struct gl843_device *dev, size_t len)
{
	dev->lbuf = realloc(dev->lbuf, len);
	if (dev->lbuf) {
		dev->lbuf_capacity = len;
		dev->lbuf_size = 0;
	}
	return dev->lbuf;
}

/* Receive pixels from the scanner.
 * buf: destination buffer
 * len: bytes to read
 * bpp: bits per pixel
 * timeout: USB timeout in milliseconds
 */
int read_pixels(struct gl843_device *dev,
		uint8_t *dst,
		size_t len,
		unsigned int bpp,
		unsigned int timeout)
{
	int ret;
	size_t n;
	uint8_t *p;

	p = dst;

	if (dev->lbuf == NULL || dev->lbuf_capacity == 0) {
		DBG(DBG_error0, "BUG: line buffer not initialized.\n");
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	while (len > 0) {
		if (dev->lbuf_size > 0) {
			int rest; /* Offset to remaining data in lbuf. */
			rest = dev->lbuf_capacity - dev->lbuf_size;

			/* Copy bytes in line buffer to caller */

			n = (len <= dev->lbuf_size) ? len : dev->lbuf_size;
			memcpy(p, dev->lbuf + rest, n);
			p += n;
			len -= n;
			dev->lbuf_size -= n;

		} else { /* lbuf_size == 0 */
			int m;
			/* Read full line from scanner. Reading odd-sized
			 * chunks could cause data loss or a stuck USB
			 * transfer. This is why read_pixels() buffers
			 * the data in the first place. */

			n = dev->lbuf_capacity;

			if (len >= dev->lbuf_capacity) {
				/* Read directly to caller buffer */
				CHK(wait_for_pixels(dev));
				CHK(m = recv_pixels(dev, p, n, bpp, timeout));
				p += m;
				len -= m;
			} else {
				/* Read into line buffer */
				CHK(wait_for_pixels(dev));
				CHK(m = recv_pixels(dev, dev->lbuf, n, bpp, timeout));
				dev->lbuf_size = m;
			}
		}
	}
	ret = 0;

chk_failed:
	return ret;
}

