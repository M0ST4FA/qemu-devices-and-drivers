#pragma once

#include "libvfio-user.h"

#define RECONNECT_MAX 5

void setup_regions_and_irqs(struct vfu_ctx *vfu_ctx);
void setup_capabilities(struct vfu_ctx *vfu_ctx);
void realize_and_connect(struct vfu_ctx *vfu_ctx);
