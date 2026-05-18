#pragma once

#include <stdio.h>

#include "libvfio-user.h"

ssize_t bar0_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count, loff_t offset, const bool is_write);
ssize_t bar1_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count, loff_t offset, const bool is_write);
