#pragma once
#include <stdint.h>

#include "common.h"
#include "libvfio-user.h"

enum device_event : uint32_t {
	EVT_INIT = 0,
	EVT_SUBMIT_JOB,
	EVT_SUBMIT_CMD,
	EVT_JOB_DONE,
	EVT_CMD_DONE,
	EVT_ERROR,
	EVT_RESET,
	EVT_COUNT,
};

extern int fsm_dispatch(vfu_ctx_t *ctx, enum device_event evt);
