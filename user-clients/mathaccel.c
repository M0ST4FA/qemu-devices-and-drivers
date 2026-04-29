#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "mathaccel/include/uapi.h" // shared header

int main([[maybe_unused]] int argc, [[maybe_unused]] char *argv[]) {
	int fd = open("/dev/mathaccel-0", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	struct mathaccel_req req = {
		.opcode = MATH_OP_ADD,
		.args = {200, 100},
	};

	if (ioctl(fd, MATHACCEL_IOC_COMPUTE, &req) < 0) {
		perror("ioctl");
		return 1;
	}

	printf("result: %llu (status: %u)\n", req.result, req.status);
	close(fd);
	return 0;
}
