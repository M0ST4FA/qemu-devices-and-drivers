#pragma once

#include <wayland-client-protocol.h>

#include "buffer.h"
#include "wayland.h"

int render_draw(struct wayland_client *client);
