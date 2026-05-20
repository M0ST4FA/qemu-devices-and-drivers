#include <bits/time.h>
#include <bits/types/sigset_t.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <syscall.h>
#include <unistd.h>

#include "logger.h"
#include "protocol.h"
#include "signal_setup.h"
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

	if (setup_signal_handlers() < 0) {
		pr_log("error", "Failed to setup signal handlers");
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
	protocol_destroy(&protocol_state);
	wayland_client_destroy(&client_state);
	return ret;
}
