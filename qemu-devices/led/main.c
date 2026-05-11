#include <bits/time.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>

#include "logger.h"
#include "wayland.h"

int main() {
	int ret = 1;
	struct wayland_client client_state = {0};

	ret = wayland_client_init(&client_state);
	if (ret < 0)
		goto cleanup;

	wayland_client_run_loop(&client_state);

	ret = 0;

cleanup:
	pr_log("error", "Error, cleaning up");
	wayland_client_destroy(&client_state);
	return ret;
}
