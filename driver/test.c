#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "util.h"
#include "low.h"
#include "cs4400f.h"
#include "scan.h"

static libusb_device_handle *open_scanner(libusb_context **ctx, int pid, int vid)
{
	int ret;
	int have_iface = 0;
	libusb_device_handle *h;

	*ctx = NULL;
	ret = libusb_init(ctx);
	if (ret < 0) {
		fprintf(stderr, "Can't initialize libusb.\n");
		return NULL;
	}
	h = libusb_open_device_with_vid_pid(NULL, pid, vid);
	if (!h) {
		fprintf(stderr, "Can't find the scanner.\n");
		return NULL;
	}

	ret = libusb_set_configuration(h, 1);
	if (ret < 0)
		goto usb_error;
	ret = libusb_claim_interface(h, 0);
	if (ret < 0)
		goto usb_error;

	return h;

usb_error:
	if (have_iface) {
		libusb_release_interface(h, 0);
	}
	if (ret < 0) {
		fprintf(stderr, "USB error. return code %d.\n", ret);
	}
	libusb_close(h);
	return NULL;
}

int main()
{

	libusb_context *ctx;
	libusb_device_handle *h;
	struct gl843_device *dev;

	init_debug("GL843", -1);
	h = open_scanner(&ctx, 0x04a9, 0x2228);
	if (!h)
		return 1;
	dev = create_gl843dev(h);

	write_reg(dev, GL843_SCANRESET, 1);
	while(!read_reg(dev, GL843_HOMESNR))
		usleep(10000);
	do_base_configuration(dev);
	set_lamp(dev, LAMP_OFF, 0);
	write_reg(dev, GL843_CLRMCNT, 1);
	write_reg(dev, GL843_CLRLNCNT, 1);
	write_reg(dev, GL843_FULLSTP, 1);

//	do_warmup_scan(dev, 0.3);
	do_move_test(dev, 10000, 24576/2, 175, 1.5, 5);

	libusb_close(dev->usbdev);
	return 0;
}
