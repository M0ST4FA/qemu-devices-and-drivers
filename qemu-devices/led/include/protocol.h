#pragma once

#include <stdint.h>
#include <sys/poll.h>

#define SERVER_SOCKET_NAME "led_grid"
#define MAX_CLIENT_CONNECTIONS 10
#define POLLFD_NR (MAX_CLIENT_CONNECTIONS + 2)

enum cmd : uint8_t {
	CMD_TOGGLE = (1 << 0),
	CMD_ON = (1 << 1),
	CMD_OFF = (1 << 2),
	CMD_SET_COLOR = (1 << 3),
};

enum event_type : uint8_t {
	LEV_ON = (1 << 0),	  // LED state became ON
	LEV_OFF = (1 << 1),	  // LED state became OFF
	LEV_COLOR = (1 << 2), // LED potentially changed color
	LEV_ERR = (1 << 3),	  // Error occured
};

enum led_err : uint16_t {
	LERR_UNKNOWN_CMD = (1 << 0),
};

// Note: only the commad to set color needs data
struct [[gnu::packed]] led_command {
	enum cmd cmd;
	int32_t led_id;
	union {
		uint8_t color[4];
	};
};
struct [[gnu::packed]] led_event {
	enum event_type ev;
	enum led_err error_code;
	int32_t led_id;
};

struct protocol_state {
	int server_fd; // fd of socket on which the server listens
	int conn_nr;   // Number of connections
	struct pollfd fds[POLLFD_NR];
};
struct led_grid;

int protocol_init(struct protocol_state *protocol_state);
void protocol_destroy(struct protocol_state *protocol_state);
int protocol_accept_connection(struct protocol_state *protocol_state);
int protocol_handle_command([[maybe_unused]] struct protocol_state *protocol_state,
							struct led_grid *led_grid,
							struct pollfd *fd);
