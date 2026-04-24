#include "include/common.h"
#include "include/setup.h"
#include "libvfio-user.h"
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage_exit(const char *prog_name) {
	printf("Usage: %s <unix_socket_path>\n", prog_name);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	int ret;
	struct math_device_state state = MATH_DEVICE_STATE_DEFAULT_INIT;

	if (argc != 2)
		print_usage_exit(argv[0]);

	const char *socket_path = argv[1];

	if (access(socket_path, F_OK) == 0) {
		printf("Socket already exists...removing it\n");
		if (unlink(socket_path) < 0)
			err(EXIT_FAILURE, "unlink: %s\n", strerror(errno));
	}

	// 1. Create the VFIO context and the UNIX socket
	printf("[HW] Initializing math-accel on %s\n", socket_path);
	vfu_ctx_t *vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, socket_path, 0, &state, VFU_DEV_TYPE_PCI);
	if (vfu_ctx == NULL) {
		if (errno == EINTR) {
			err(EXIT_FAILURE, "%s\n", "Interrupted");
		} else {
			err(EXIT_FAILURE, "%s\n", "Failed to initialize device emulation");
		}
	}

	// 2. Setup the basic PCI header
	ret = vfu_pci_init(vfu_ctx, VFU_PCI_TYPE_CONVENTIONAL, PCI_HEADER_TYPE_NORMAL, 0);
	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to initialize PCI");

	vfu_pci_set_id(vfu_ctx, 0x1234, 0x5678, 0, 0);

	setup_bars_and_irqs(vfu_ctx);

	setup_capabilities(vfu_ctx);

	// NOTE: contains the event loop
	realize_and_connect(vfu_ctx);

	vfu_destroy_ctx(vfu_ctx);

	return 0;
}
