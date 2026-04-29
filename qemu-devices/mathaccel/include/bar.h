#pragma once

#include "libvfio-user.h"

ssize_t bar0_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count, loff_t offset, const bool is_write);
