#include <errno.h>
#include <linux/pci_regs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "device.h"
#include "logger.h"

static inline void print_usage_exit(const char *prog_name) {
	printf("Usage: %s <f0_sock_path> <f1_sock_path>\n", prog_name);
	exit(EXIT_FAILURE);
}

static inline int parse_cmd_arguments(char *argv[], struct lux_silicon *lux_silicon) {
	lux_silicon->f0_sock_path = argv[1];
	lux_silicon->f1_sock_path = argv[2];

	if (access(lux_silicon->f0_sock_path, F_OK) == 0) {
		pr_log("debug", "%s: Socket already exists...removing it", lux_silicon->f0_sock_path);

		if (unlink(lux_silicon->f0_sock_path) < 0) {
			pr_log_libcerror(errno, "unlink");
			return -1;
		}
	}

	if (access(lux_silicon->f1_sock_path, F_OK) == 0) {
		pr_log("debug", "%s: Socket already exists...removing it", lux_silicon->f1_sock_path);

		if (unlink(lux_silicon->f1_sock_path) < 0) {
			pr_log_libcerror(errno, "unlink");
			return -1;
		}
	}

	return 0;
}

int main(int argc, char *argv[]) {
	int ret = 0;
	struct lux_silicon device = {0};

	if (argc != 3)
		print_usage_exit(argv[0]);

	ret = parse_cmd_arguments(argv, &device);
	if (ret < 0)
		goto cleanup;

	ret = device_init(&device,
					  device.f0_sock_path,
					  device.f1_sock_path);
	if (ret < 0) {
		pr_log("error", "Failed to initialize device");
		goto cleanup;
	}

	ret = device_run_eventloop(&device);
	if (ret < 0) {
		pr_log("error", "Error while executing inside event loop");
		goto cleanup;
	}

	device_destroy(&device);
	ret = 0;

cleanup:
	pr_log("debug", "Cleaning up and closing...");
	return ret;
}
