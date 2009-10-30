

struct data_block
{
	struct data_block *next;
	size_t size;
	uint8_t buf[0];
};

struct reader_task_info
{
	int pid;	/* Task ID */
	int rd_pipe;
	int wr_pipe;
	size_t rd_size;	/* Total number of bytes to read from scanner. */
	size_t rd_left;	/* Number of bytes left to read from scanner. */
	struct libusb_device_handle *usbdev;

	struct data_block *q_head, *q_tail;
};

/* Read a block of bulk data from the scanner, and enqueue it. */
static int read_and_enqueue(struct reader_task_info *this, int len)
{
	struct data_block *blk;
	const int to = 1000;	/* USB timeout [ms] */
	int outlen;
	int ret;

	/* Read a data block from scanner. */
	blk = malloc(sizeof(*blk) + len);
	if (!blk)
		return LIBUSB_ERROR_NO_MEM;
	ret = libusb_bulk_transfer(this->usbdev, 0x81, blk->buf, len, &outlen, to);
	blk->size = outlen;

	/* Enqueue the data block. Check for errors later. */
	if (this->q_tail)
		this->q_tail->next = blk;
	blk->next = NULL;
	this->q_tail = blk;
	if (!this->q_head)
		this->q_head = this->q_tail;

	return ret;
}

/* Try writing a data block in the queue. Dequeue and free it if successful. */
static int write_and_dequeue(struct reader_task_info *this)
{
	int ret;
	struct data_block *blk;

	blk = this->q_head;
	if (blk) {
		/* Write data block at queue tail to pipe. */
		ret = write(this->wr_pipe, blk->buf, blk->size);
		if (ret != EAGAIN) {
			/* Write not stalled - dequeue data block */
			this->q_head = blk->next;
			if (!this->q_head)
				this->q_tail = NULL;
			free(blk);
		}
	}
	return ret;
}

/* This process/thread reads and buffers image data from the scanner. */
static int reader_task(void *args)
{
	int ret;
	uint8_t tmp;
	struct reader_task_info *this = (struct reader_task_info *) args;

	/* Note: This task does not free its memory if killed by the parent.
	 * This is by design. It is assumed the system will reclaim the
	 * memory when the process or thread exits or is detached.
	 */

	/* Read and buffer bulk data from the scanner. */

	while (this->rd_left > 0) {
		int len = this->rd_left < 16384 ? this->rd_left : 16384;

		ret = read_and_enqueue(this, len);
		if (ret == LIBUSB_ERROR_NO_MEM) {
			DBG(DBG_error, "out of memory\n");
			return -ENOMEM;
		}

		this->rd_left -= this->q_tail->size;

		DBG(DBG_io, "requested %d, received %zu bytes. (%zu left)\n",
			len, this->q_tail->size, this->rd_left);

		/* Exit when parent sends something. Currently unused. */
		if (read(this->rd_pipe, &tmp, 1) == 1)
			return 0;

		if (ret == LIBUSB_ERROR_INTERRUPTED)
				continue;
		if (ret < 0) {
			DBG(DBG_error, "libusb error: %s\n",
				sanei_libusb_strerror(ret));
			return -EIO;
		}

		ret = write_and_dequeue(this);
		if (ret != EAGAIN && ret != 0) {
			DBG(DBG_error, "bulk I/O error: %s\n", strerror(errno));
			return -errno;
		}
	}

	/* Wait until parent has read all buffers. */

	while (this->q_head) {
		ret = write_and_dequeue(this);
		if (ret == EAGAIN) {
			/* pipe full */
			usleep(1000);
		} else if (ret != 0) {
			DBG(DBG_error, "pipe I/O error: %s\n",
				strerror(errno));
			return -errno;
		}
	}

	return 0;
}

int create_reader_task(struct reader_task_info *this,
		       size_t rd_size,
		       struct libusb_device_handle *usbdev)
{
	int ret;
	int pfd[2];

	this->rd_size = rd_size;
	this->rd_left = rd_size;
	this->usbdev = usbdev;
	this->q_head = NULL;
	this->q_tail = NULL;

	ret = pipe2(pfd, O_NONBLOCK);
	if (ret < 0)
		goto pipe_fail;

	this->rd_pipe = pfd[0];
	this->wr_pipe = pfd[1];

	this->pid = sanei_thread_begin(reader_task, this);
	if (this->pid < 0)
		goto task_fail;

	return 0;

pipe_fail:
	DBG(DBG_error, "Cannot open scanner bulk transfer pipe: %s\n",
		strerror(errno));
	return -1;

task_fail:
	close(pfd[0]);
	close(pfd[1]);
	DBG(DBG_error, "Cannot start scanner bulk transfer worker thread: %s\n",
		strerror(errno));
	return -1;
}
