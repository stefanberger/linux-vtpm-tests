/*
 * Open many server-side file descriptors and exit without closing them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <linux/vtpm_proxy.h>

int spawn_device(struct vtpm_proxy_new_dev *vtpm_new_dev)
{
	int fd, n;

	fd = open("/dev/vtpmx", O_RDWR);
	if (fd < 0) {
		perror("Could not open /dev/vtpmx");
		return 1;
	}

	n = ioctl(fd, VTPM_PROXY_IOC_NEW_DEV, vtpm_new_dev);
	if (n != 0) {
		perror("ioctl to create new device failed");
		close(fd);
		return 1;
	}

	close(fd);

	return 0;
}

#define NUM_DEVICES	64
int main(void)
{
	struct vtpm_proxy_new_dev vtpm_new_dev;
	size_t c;

	for (c = 0; c < NUM_DEVICES; c++) {
		if (spawn_device(&vtpm_new_dev)) {
			fprintf(stderr, "Could not create device.\n");
			return 1;
		}
	}
	fprintf(stdout, "Opened %d devices.\n", c);

	return 0;
}
