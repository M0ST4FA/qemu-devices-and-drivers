#pragma once

#include "device.h"

irqreturn_t mathaccel_irq_handler(int irq, void *cookie);
void mathaccel_irq_read_legacy_cmd_result(struct mathaccel_device *dev);
void mathaccel_irq_consume_completion_queue(struct mathaccel_device *dev);
void mathaccel_irq_handle_error(struct mathaccel_device *dev);
