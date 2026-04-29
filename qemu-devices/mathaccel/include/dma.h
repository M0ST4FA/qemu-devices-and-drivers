#pragma once

#include "common.h"
#include "libvfio-user.h"

int dma_read_next(struct vfu_ctx *ctx, struct math_sq_entry *cmd);
int dma_write_next(struct vfu_ctx *ctx, struct math_cq_entry *res);
