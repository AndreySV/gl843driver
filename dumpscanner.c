/*  dumpscanner.c - a tool for capturing data to/from GL84x based scanners

    Copyright (C) 2009  Andreas Robinson <andr345 at gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <ansidecl.h>

#define SETUP_LEN 8

#define MON_IOC_MAGIC 0x92
#define MON_IOCG_STATS _IOR(MON_IOC_MAGIC, 3, struct usbmon_stats)
#define MON_IOCT_RING_SIZE _IO(MON_IOC_MAGIC, 4)
#define MON_IOCX_MFETCH _IOWR(MON_IOC_MAGIC, 7, struct usbmon_mfetch)
#define MON_IOCH_MFLUSH _IO(MON_IOC_MAGIC, 8)

#define SCAN_UNDEF 'x'
#define SCAN_RD_REG 'r'
#define SCAN_WR_REG 'w'
#define SCAN_SEL_REG 's'
#define SCAN_WR_BYTES 'd'
#define SCAN_RD_BULK 'R'
#define SCAN_WR_BULK 'W'
#define SCAN_RD_ACK 'a'
#define SCAN_WR_ACK 'b'
#define SCAN_RD_BULK_ACK 'A'
#define SCAN_WR_BULK_ACK 'B'

struct usbmon_packet {
	uint64_t id;		/* URB ID - from submission to callback */
	unsigned char type;	/* S = submit, C = complete, E = error */
	unsigned char xfer_type;/* ISO (0), Intr, Control, Bulk (3) */
	unsigned char epnum;	/* Endpoint number and transfer direction */
	unsigned char devnum;	/* Device address */
	unsigned short busnum;	/* Bus number */
	char flag_setup;
	char flag_data;
	int64_t ts_sec;		/* gettimeofday */
	int32_t ts_usec;	/* gettimeofday */
	int status;
	unsigned int len_urb;	/* Length of data (submitted or actual) */
	unsigned int len_cap;	/* Delivered length */
	union {
		unsigned char setup[SETUP_LEN];	/* Only for Control S-type */
		struct iso_rec {
			int error_count;
			int numdesc;
		} iso;
	} s;
	int interval;
	int start_frame;
	unsigned int xfer_flags;
	unsigned int ndesc;	/* Actual number of ISO descriptors */
};

struct usbmon_stats {
	uint32_t queued;
	uint32_t dropped;
};

struct usbmon_mfetch {
	uint32_t *offvec;	/* Vector of events fetched */
	uint32_t nfetch;	/* Number of events to fetch (out: fetched) */
	uint32_t nflush;	/* Number of events to flush */
};

/* Read numerical sysfs attribute into a buffer.
 * returns: >= 0 = ok, -1 = attribute missing,
 * -2 = file error (or empty file).
 */
int get_sysfs_attrib(const char *sysfs_path,
		     const char *dev_path,
		     const char *name,
		     int base)
{
	char *filename;
	FILE *file;

	const size_t BUFLEN = 16;
	char buf[BUFLEN];
	char *s;

	asprintf(&filename, "%s/%s/%s", sysfs_path, dev_path, name);
	file = fopen(filename, "r");
	if (file == NULL) {
		free(filename);
		return -1;
	}
	s = fgets(buf, BUFLEN, file);
	fclose(file);
	free(filename);

	return (s != 0) ? (int) strtol(buf, NULL, base) : -2;
}

/* Find the first USB device with the given vendor and id.
 * Returns USB bus and device numbers.
 */
int find_usb_device(int vendor, int id, int *bus, int *dev)
{
	const char *path = "/sys/bus/usb/devices";
	DIR *dir;
	struct dirent *de;
	int found = 0;

	*bus = 0;
	*dev = 0;

	dir = opendir(path);
	if (!dir) {
		fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
		return 0;
	}

	while ((de = readdir(dir)) != NULL) {
		int vend, prod;

		if (strchr(de->d_name, '-') == NULL)
			continue;

		vend = get_sysfs_attrib(path, de->d_name, "idVendor", 16);
		prod = get_sysfs_attrib(path, de->d_name, "idProduct", 16);

		if (vend == vendor && prod == id) {
			*bus = get_sysfs_attrib(path, de->d_name, "busnum", 10);
			*dev = get_sysfs_attrib(path, de->d_name, "devnum", 10);
			found = 1;
			break;
		}
	}
	closedir(dir);
	return found;
}

/* Parse and log scanner commands and bulk data.
 * Written for the GL84x scanner controller.
 */
int process_urb(struct usbmon_packet *hdr, unsigned char *data, FILE *file)
{
	/* Z = ISO, I = interrupt, C = ctrl, B = bulk */
	const char typenames[4] = { 'Z', 'I', 'C', 'B' };

	int ev = hdr->type;	/* event type (S, C or E) */
	int type = typenames[hdr->xfer_type & 3];
	int dir = ((hdr->epnum & 0x80) != 0) ? 'i' : 'o';
	int ep = hdr->epnum & 0xf;
	int has_setup = hdr->flag_setup == 0;
	int len_urb = hdr->len_urb;

	uint32_t ts = (unsigned) (hdr->ts_sec * 1000 + hdr->ts_usec / 1000);
	uint8_t cmd = SCAN_UNDEF;
	unsigned char *buf = NULL;
	int32_t blen = 0;

	//printf ("%c:%c%c:%d", ev, type, dir, ep);

	/* Look for known URBs for the GL84x scanner controller */

	if (ev == 'S' && type == 'C' && ep == 0 && has_setup) {
		int rtype = hdr->s.setup[0];
		int req = hdr->s.setup[1];
		int val = (hdr->s.setup[3] << 8) | hdr->s.setup[2];
		int idx = (hdr->s.setup[5] << 8) | hdr->s.setup[4];
		int len = (hdr->s.setup[7] << 8) | hdr->s.setup[6];

		//printf(" s %02x %02x %04x %04x %04x",
		//	rtype, req, val, idx, len);

		if (dir == 'i' && rtype == 0xC0 && req == 0x0C
			&& val == 0x84 && idx == 0 && len == 1)
		{
			/* Request value of selected scanner IO register */
			cmd = SCAN_RD_REG;
			buf = data;
			blen = len;

		} else if (dir == 'o' && rtype == 0x40 && req == 0x04
			&& val == 0x83 && idx == 0 && len == 2)
		{
			/* Write byte to scanner IO register */
			cmd = SCAN_WR_REG;
			buf = data;
			blen = len;

		} else if (dir == 'o' && rtype == 0x40 && req == 0x0c
			&& val == 0x83 && idx == 0 && len == 1)
		{
			/* Select scanner IO register */
			cmd = SCAN_SEL_REG;
			buf = data;
			blen = len;

		} else if (dir == 'o' && rtype == 0x40 && req == 0x04
			&& val == 0x82 && idx == 0)
		{
			/* Write bytes to selected scanner IO register */
			cmd = SCAN_WR_BYTES;
			buf = data;
			blen = len;
		}
	} else if (ev == 'S' && type == 'B') {
		if (dir == 'i' && ep == 1) {
			/* Request bulk data from scanner */
			cmd = SCAN_RD_BULK;
			buf = NULL;
			blen = len_urb;

		} else if (dir == 'o' && ep == 2) {
			/* Send bulk data to scanner */
			cmd = SCAN_WR_BULK;
			buf = data;
			blen = len_urb;
		}
	} else if (ev == 'C' && type == 'C' && ep == 0) {
		if (dir == 'i') {
			/* Scanner returns data */
			cmd = SCAN_RD_ACK;
			buf = data;
			blen = len_urb;
		} else /* if (dir == 'o') */ {
			/* Scanner acks receiving data */
			cmd = SCAN_WR_ACK;
			buf = NULL;
			blen = len_urb;
		}
	} else if (ev == 'C' && type == 'B') {
		if (dir == 'i' && ep == 1) {
			/* Scanner returns bulk data */
			cmd = SCAN_RD_BULK_ACK;
			buf = data;
			blen = len_urb;
		} else if (dir  == 'o' && ep == 2) {
			/* Scanner acks receiving bulk data */
			cmd = SCAN_WR_BULK_ACK;
			buf = NULL;
			blen = len_urb;
		}
	}
	if (cmd == SCAN_UNDEF) {
		fprintf(stderr, "Unknown URB: %c%c%c:%d",
			ev, type, dir, ep);
		if (has_setup) {
			/* Unhandled URB with setup,
			 * save usbmon packet header and data. */
			int rtype = hdr->s.setup[0];
			int req = hdr->s.setup[1];
			int val = (hdr->s.setup[3] << 8) | hdr->s.setup[2];
			int idx = (hdr->s.setup[5] << 8) | hdr->s.setup[4];
			int len = (hdr->s.setup[7] << 8) | hdr->s.setup[6];

			fprintf(stderr, " s %02x %02x %04x %04x %04x",
				rtype, req, val, idx, len);

			if (len > 0) {
				fprintf(stderr, " =");
				for (i = 0; i < len; i++) {
					fprintf(stderr, " 0x%02x", data[i]);
				}
			}
			buf = (uint8_t *) hdr;
			blen = sizeof(*hdr) + len;
		} else {
			/* Unhandled URB without a setup,
			 * just save the usbmon packet header */
			buf = (uint8_t *) hdr;
			blen = sizeof(*hdr);
		}
		fprintf(stderr, "\n");
	}

	/* Write the parsed scanner command to disk */

	char scan_cmd[] = {
		(ts >> 24) & 0xff, (ts >> 16) & 0xff,
		(ts >> 8) & 0xff, ts & 0xff, cmd, (buf != NULL) ? 1 : 0,
		(blen & 0xff00) >> 8, (blen & 0xff) };
	int r;

	r = fwrite(scan_cmd, 1, sizeof(scan_cmd), file);
	if (r != sizeof(scan_cmd))
		goto file_error;
	if (blen > 0 && buf != NULL) {
		r = fwrite(buf, 1, blen, file);
		if (r != blen)
			goto file_error;
	}
	return 1;
file_error:
	fprintf(stderr, "Error writing logfile: %s\n", strerror(errno));
	return 0;
}

int d = -1;		/* /dev/usbmon* descriptor */
FILE *logfile = NULL;	/* Output file */
int ntotal = 0;		/* Number of packets processed */

void sigint_handler(ARG_UNUSED(int sig))
{
	if (d > 0) {
		struct usbmon_stats stats;
		ioctl(d, MON_IOCG_STATS, &stats);
		printf("\nStopped by user. Processed %d events, %d dropped.\n",
			ntotal, stats.dropped);
		/* Fixme: Unmap buffer... */
		close(d);
	}
	if (logfile)
		fclose(logfile);
	exit(0);
}

int main()
{
	int busnum, devnum;
	char *filename;
	uint8_t *buf;
	const size_t BUFLEN = 128*1024;

	/* Edit these settings.  TODO: Use command line args. */
	int vend = 0x04a9;
	int prod = 0x2228;
	const char logname[] = "log.bin";

	signal(SIGINT, sigint_handler);

	/* Check we're root */

	if (getuid() != 0) {
		fprintf(stderr, "Not running as root. Exiting.\n");
		return 1;
	}

	/* Look for USB device */

	if (!find_usb_device(vend, prod, &busnum, &devnum)) {
		fprintf(stderr, "Device %04x:%04x not found.\n", vend, prod);
		return 1;
	}
	fprintf(stderr, "Found device: bus %d, device %d\n", busnum, devnum);

	/* Open usbmon */

	asprintf(&filename, "/dev/usbmon%d", busnum);
	d = open(filename, O_RDWR);
	if (d < 0) {
		fprintf(stderr, "Cannot open %s: %s\n", filename, strerror(errno));
		return 1;
	}
	free(filename);

	/* Prepare usbmon ring-buffer */

	if (ioctl(d, MON_IOCT_RING_SIZE, BUFLEN) < 0) {
		fprintf(stderr, "Cannot allocate ring buffer: %s\n", strerror(errno));
		return 1;
	}

	buf = mmap(NULL, BUFLEN, PROT_READ, MAP_SHARED, d, 0);
	if (buf == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap ring buffer: %s\n", strerror(errno));
		return 1;
	}

	/* Open log file. */

	logfile = fopen(logname, "w+");
	if (!logfile) {
		fprintf(stderr, "Cannot open %s: %s\n", logname, strerror(errno));
		return 1;
	}

	/* Main capture loop */

	const size_t PCKCNT = 100;
	uint32_t offvec[PCKCNT];
	int i, nflush = 0;
	struct usbmon_mfetch fetch;
	struct usbmon_packet *hdr;

	for (;;) {

		/* Fetch packets */

		fetch.offvec = offvec;
		fetch.nfetch = PCKCNT;
		fetch.nflush = nflush;
		if (ioctl(d, MON_IOCX_MFETCH, &fetch) == -1) {
			fprintf(stderr, "usbmon read error: %s\n", strerror(errno));
			return 1;
		}
		nflush = fetch.nfetch;
		ntotal += nflush;

		/* Process packets to/from busnum:devnum */

		for (i = 0; i < nflush; i++) {
			hdr = (struct usbmon_packet *) &buf[offvec[i]];
			if (hdr->type == '@' || hdr->devnum != devnum)
				continue;

			process_urb(hdr, ((unsigned char *)hdr) + sizeof(*hdr), logfile);
		}
	}

	/* Dead code */
	return 0;
}
