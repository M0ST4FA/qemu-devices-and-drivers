#include <errno.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "logger.h"
#include "protocol.h"
#include "wayland.h"

/* Starts listening on a UNIX domain socket
 * @returns fd of socket on which we listen on success. -1 on error.
 * */
int protocol_init(struct protocol_state *protocol_state) {
	int server_fd, ret = 0;

	// 1. Create socket
	server_fd = socket(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (server_fd < 0) {
		pr_log_libcerror(errno, "socket");
		return -1;
	}

	// 2. Bind socket to a name
	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	// We use an abstract unix-domain socket
	strncpy(&addr.sun_path[1], SERVER_SOCKET_NAME, sizeof(addr.sun_path) - 2);

	ret = bind(server_fd, &addr, sizeof(addr));
	if (ret < 0) {
		pr_log_libcerror(errno, "bind");
		return -1;
	}

	// 3. Set socket state to "listening"
	ret = listen(server_fd, 5);
	if (ret < 0) {
		pr_log_libcerror(errno, "listen");
		return -1;
	}
	protocol_state->server_fd = server_fd;
	protocol_state->conn_nr = 0;

	pr_log("debug", "Started listening on abstract socket: %s", addr.sun_path);

	return server_fd;
}

void protocol_destroy(struct protocol_state *protocol_state) {
	if (protocol_state->server_fd > STDERR_FILENO) {
		close(protocol_state->server_fd);
		protocol_state->server_fd = -1;
	}

	struct pollfd *fds = protocol_state->fds;
	// Client connections are stored starting at index 2.
	// Index 0 is Wayland FD, Index 1 is server_fd.
	for (int i = 2; i < POLLFD_NR; i++) {
		if (fds[i].fd > STDERR_FILENO) {
			if (close(fds[i].fd) < 0)
				pr_log_libcerror(errno, "close(protocol_destroy)");
			fds[i].fd = -1;
		}
	}
}

int protocol_accept_connection(struct protocol_state *protocol_state) {
	struct pollfd *fds = protocol_state->fds;

	if (protocol_state->conn_nr > MAX_CLIENT_CONNECTIONS) {
		pr_log("debug", "Rejecting new client connection: maximum number of connections reached");
		return -1;
	}

	int new_fd = accept4(protocol_state->server_fd, NULL, NULL, SOCK_NONBLOCK);
	if (new_fd < 0) {
		pr_log_libcerror(errno, "accept4");
		return -1;
	}

	// Find an empty slot and use it; it MUST exist because we haven't reached connection limit yet
	for (int i = 2; i < POLLFD_NR; i++) {
		struct pollfd *current = &fds[i];
		if (current->fd < 0) {
			current->fd = new_fd;
			current->events = POLLIN;
			protocol_state->conn_nr++;
			break;
		}
	}

	pr_log("debug", "A new client has connected!");
	return new_fd;
}

int protocol_handle_command([[maybe_unused]] struct protocol_state *protocol_state,
							struct led_grid *led_grid,
							struct pollfd *fd) {
	struct led_command cmd = {0};
	struct led_event ev = {0};
	struct led *led = NULL;
	int n = read(fd->fd, &cmd, sizeof(cmd));

	// 1. Check for errors
	if (n < 0) {
		if (fd->revents & POLLNVAL) {
			pr_log("error", "Invalid event set for fd %d", fd->fd);
			return -1;
		}

		if (fd->revents & POLLERR) {
			pr_log("debug", "Error happened on fd %d", fd->fd);
			return -1;
		}
	}

	// 2. Check for spurious calls
	if (fd->revents == 0) {
		pr_log("debug", "protocol_handle_command: Spurious call, no event to handle");
		return 0;
	}

	// 3. Check for disconnecting clients
	if (n == 0 || fd->revents & POLLHUP) {
		pr_log("debug", "Client disconnected!");
		if (close(fd->fd) < 0)
			pr_log_libcerror(errno, "close(protocol_handle_command)");

		fd->fd = -1;
		fd->events = 0;
		protocol_state->conn_nr--;
		return 0;
	}

	// 4. Handle command
	if (n != sizeof(cmd))
		return -1;

	if (cmd.led_id < LED_NR)
		led = &led_grid->leds[cmd.led_id];
	else {
		pr_log("debug", "Invalid LED ID: Out of range");
		return -1;
	}

	ev.led_id = cmd.led_id;

	switch (cmd.cmd) {
		case CMD_TOGGLE:
			led->on = !led->on;
			ev.ev = led->on ? LEV_ON : LEV_OFF;
			break;

		case CMD_ON:
			led->on = 1;
			ev.ev = LEV_ON;
			break;

		case CMD_OFF:
			led->on = 0;
			ev.ev = LEV_OFF;
			break;

		case CMD_SET_COLOR:
			memcpy(led->color, cmd.color, sizeof(led->color));
			ev.ev = LEV_COLOR;
			break;

		default:
			pr_log("error", "Unkown command <%d>", cmd.cmd);
			ev.ev = LEV_ERR;
			ev.error_code = LERR_UNKNOWN_CMD;
			return -1;
	}

	if (write(fd->fd, &ev, sizeof(ev)) != sizeof(ev))
		pr_log("error", "Failed to send event");

	const char *cmd_to_str[] = {
		[CMD_SET_COLOR] = "CMD_SET_COLOR",
		[CMD_ON] = "CMD_ON",
		[CMD_OFF] = "CMD_OFF",
		[CMD_TOGGLE] = "CMD_TOGGLE",
	};
	const char *ev_to_str[] = {
		[LEV_ON] = "LEV_ON",
		[LEV_OFF] = "LEV_OFF",
		[LEV_COLOR] = "LEV_COLOR",
		[LEV_ERR] = "LEV_ERR",
	};

	pr_log("debug", "Received command: CMD %s, LED ID %d", cmd_to_str[cmd.cmd], cmd.led_id);
	pr_log("debug", "Sent event: EV %s, LED ID %d, ERR: %d",
		   ev_to_str[ev.ev], ev.led_id, ev.error_code);

	return 0;
}
