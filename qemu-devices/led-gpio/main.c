#include <errno.h>
#include <linux/pci_regs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "device.h"
#include "logger.h"

static inline void print_usage_exit(const char *prog_name) {
	printf("Usage: %s <unix_socket_path>\n", prog_name);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	int ret;
	char *socket_path;
	struct led_grid_device device = {0};

	if (argc != 2)
		print_usage_exit(argv[0]);

	socket_path = argv[1];
	if (access(socket_path, F_OK) == 0) {
		pr_log("debug", "Socket already exists...removing it");

		if (unlink(socket_path) < 0) {
			pr_log_libcerror(errno, "unlink");

			ret = -1;
			goto cleanup;
		}
	}

	ret = device_init(&device, socket_path);
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
