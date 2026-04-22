#include "include/edu.h"
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#define PATH_SIZE 255

const char *usage = "Usage: \n\
	pci-user <edu-pci dev name> <operation>\
\n\
Operation: \n\
	one of: identity or liveness \
\n";

int main(int argc, char **argv) {

	char *node_path = NULL, *op = NULL;
	int fd;
	int32_t val;

	if (argc < 3)
		err(EXIT_FAILURE, "%s\n", usage);

	node_path = argv[1];
	op = argv[2];

	printf("Opening file: %s\nOperation: %s\n", node_path, op);

	fd = open(node_path, O_CLOEXEC | O_RDWR | O_SYNC);
	if (fd < 0)
		err(EXIT_FAILURE, "open: %s\n", strerror(errno));

	if (!strncmp("identity", op, sizeof("identity"))) {
		if (ioctl(fd, EDU_IOCTL_IDENT, &val) < 0)
			err(EXIT_FAILURE, "ioctl(identity): %s\n", strerror(errno));

		printf("Identity: %08X\n", val);
	} else if (!strncmp("liveness", op, sizeof("liveness"))) {
		val = 0xf0f0; // Set initial value
		printf("Initial value: %08X\n", val);
		if (ioctl(fd, EDU_IOCTL_LIVENESS, &val) < 0)
			err(EXIT_FAILURE, "ioctl(liveness): %s\n", strerror(errno));

		printf("Read value: %08X\n", val);
	} else
		err(EXIT_FAILURE, "%s\n", "Invalid operation");

	return 0;
}
