#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include "logger.h"
#include "signal_setup.h"
#include "wayland.h"

#define SIGSTACK_SZ 32000

uint8_t signal_stack[SIGSTACK_SZ] = {0};
int sigpipe[2] = {0};

void handle_sigusr1([[maybe_unused]] int sig, siginfo_t *siginfo, [[maybe_unused]] void *c) {
	paused = !paused;
	pr_log("debug", "Received SIGUSR1 (%d, cause code: %d), paused: %d", siginfo->si_signo, siginfo->si_code, paused);
	write(sigpipe[1], "done", sizeof("done"));
};

int setup_signal_handlers() {
	if (pipe(sigpipe) < 0) {
		pr_log_libcerror(errno, "pipe");
		return -1;
	};

	// CREATE STRUCT
	struct sigaction sa = {
		.sa_sigaction = handle_sigusr1,
		.sa_flags = SA_ONSTACK | SA_SIGINFO,
	};
	sigemptyset(&sa.sa_mask);

	// SETUP SIGNAL STACK
	stack_t ss = {
		.ss_sp = signal_stack,
		.ss_size = SIGSTACK_SZ - 1,
		.ss_flags = 0,
	};
	if (sigaltstack(&ss, NULL) < 0) {
		pr_log_libcerror(errno, "signalstack");
		return -1;
	};

	// REGISTER ACTION
	signal(SIGPIPE, SIG_IGN);
	signal(SIGBUS, SIG_IGN);
	signal(SIGILL, SIG_IGN);

	if (sigaction(SIGUSR1, &sa, NULL) < 0) {
		pr_log_libcerror(errno, "signal");
		return -1;
	}

	// MASK SOME SIGANSL FOR RACE CONDITION CONSIDERATIONS
	sigset_t block_mask;
	sigemptyset(&block_mask);
	sigaddset(&block_mask, SIGUSR1);

	if (sigprocmask(SIG_BLOCK, &block_mask, NULL) < 0) {
		pr_log_libcerror(errno, "sigprocmask");
		return -1;
	}

	return 0;
}
