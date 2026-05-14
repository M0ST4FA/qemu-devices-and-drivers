#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "logger.h"
#include "protocol.h"

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

	pr_log("debug", "Started listening on abstract socket: %s", addr.sun_path);

	return server_fd;
}

void protocol_destroy(struct protocol_state *protocol_state) {
	if (protocol_state->server_fd > STDERR_FILENO)
		close(protocol_state->server_fd);
}
