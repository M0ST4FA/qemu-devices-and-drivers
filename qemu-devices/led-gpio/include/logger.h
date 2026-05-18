#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

[[gnu::format(printf, 2, 3)]]
static inline void pr_log(const char *level, const char *format, ...) {
	va_list args;

	fprintf(stderr, "[%s] ", level);

	va_start(args, format);

	vfprintf(stderr, format, args);

	va_end(args);

	fprintf(stderr, "\n");
}

static inline void pr_log_libcerror(int err, const char *msg) {
	pr_log("error", "%s: %s", msg, strerror(err));
}
