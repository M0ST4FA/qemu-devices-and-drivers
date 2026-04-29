#pragma once

#include "common.h"
#include "libvfio-user.h"

int execute_operation(struct math_sq_entry *cmd, struct math_cq_entry *res);
int consume_submission_queue(struct vfu_ctx *ctx);
