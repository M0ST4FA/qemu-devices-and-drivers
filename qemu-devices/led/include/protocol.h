#pragma once

#include <stdint.h>

#define SERVER_SOCKET_NAME "led_grid"

enum cmd : uint8_t {
	CMD_TOGGLE = (1 << 0),
	CMD_ON = (1 << 1),
	CMD_OFF = (1 << 2),
	CMD_SET_COLOR = (1 << 3),
};

// Note: only the commad to set color needs data
struct [[gnu::packed]] led_command {
	enum cmd cmd;
	int32_t led_id;
	union {
		uint8_t color[4];
	};
};

struct protocol_state {
	int server_fd;
};

int protocol_init(struct protocol_state *protocol_state);
void protocol_destroy(struct protocol_state *protocol_state);
