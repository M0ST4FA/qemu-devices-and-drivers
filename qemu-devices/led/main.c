#include <bits/time.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>

#include "logger.h"
#include "protocol.h"
#include "wayland.h"

int main() {
	int ret = 1;
	struct protocol_state protocol_state = {0};
	struct wayland_client client_state = {0};

	ret = protocol_init(&protocol_state);
	if (ret < 0) {
		pr_log("error", "Failed to setup LED protocol");
		goto cleanup;
	}

	ret = wayland_client_init(&client_state);
	if (ret < 0) {
		pr_log("error", "Failed to setup wayland client");
		goto cleanup;
	}

	ret = wayland_client_run_loop(&client_state, &protocol_state);
	if (ret < 0) {
		pr_log("error", "Error while running event loop");
		goto cleanup;
	}

	ret = 0;

cleanup:
	pr_log("debug", "Cleaning up");
	wayland_client_destroy(&client_state);
	protocol_destroy(&protocol_state);
	return ret;
}
