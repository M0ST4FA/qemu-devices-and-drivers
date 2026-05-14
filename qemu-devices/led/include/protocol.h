#pragma once

#include <stdint.h>

// enum cmd : uint8_t {
// 	CMD_ON = (1 << 0),
// 	CMD_OFF = (1 << 1),
// };
//
// struct [[gnu::packed]] led_command {
// 	enum cmd cmd;
// 	uint8_t led_id;
// 	uint8_t color[4]; // RGBA
// };

enum cmd : uint8_t {
	CMD_IMPULSE = (1 << 0),
	CMD_SET_COLOR = (1 << 0),
};

struct [[gnu::packed]] ball_impulse {
	float dx, dy;
};

struct [[gnu::packed]] ball_command {
	enum cmd cmd;
	union {
		struct ball_impulse impulse;
		uint8_t color[4];
	} data;
};

struct protocol_state {
	int server_fd;
};

int protocol_init(struct protocol_state *protocol_state);
void protocol_destroy(struct protocol_state *protocol_state);
