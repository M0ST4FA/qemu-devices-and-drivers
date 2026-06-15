#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../qemu-drivers/led-gpio/include/lux_ioctl.h"

#define ALM_SIG (SIGRTMIN)

static void signal_handler(int signo, siginfo_t *si, void *ctx) {
	printf("Received signal %d...\n", ALM_SIG);
}

static inline void install_signal_handler(int fd, int pid) {
	struct sigaction sa = {0};
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = signal_handler;

	if (sigaction(ALM_SIG, &sa, NULL) < 0)
		err(EXIT_FAILURE, "[%d] sigaction", pid);
}

static inline void print_usage_exit(void) {
	printf("Usage: timer-test <child-nr>\n");
	exit(EXIT_SUCCESS);
}

static int timer_test_time(int fd, int pid) {
	__u64 now;

	if (ioctl(fd, LUX_TIME_RD, &now) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_TIME_RD", pid);

	printf("[%d] Time now: %llu\n", pid, now);

	now += 5000;
	if (ioctl(fd, LUX_TIME_SET, &now) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_TIME_SET", pid);
	printf("[%d] Time set to: %llu\n", pid, now);

	return now;
}

static int timer_test_alarm(int fd, int pid, __u64 alarm) {
	__u64 overruns = 0;

	if (ioctl(fd, LUX_ALM_SET, &alarm) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_ALM_SET", pid);
	printf("[%d] Alarm set to: %llu\n", pid, alarm);
	if (ioctl(fd, LUX_AIE_ON) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_AIE_ON", pid);

	if (ioctl(fd, LUX_ALM_RD, &alarm) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_ALM_RD", pid);
	printf("[%d] Alarm: %llu\n", pid, alarm);

	sleep(2); // Accumulate overruns
	if (read(fd, &overruns, sizeof(overruns)) < 0)
		err(EXIT_FAILURE, "[%d] read", pid);
	printf("[%d] Alarm fired! Overruns: %llu, cause: %llu\n",
		   pid, LUX_CLOCK_OVERRUN(overruns), LUX_CLOCK_IRQ_CAUSE(overruns));

	return overruns;
}

static int timer_test_alarm_periodic(int fd, int pid, __u64 alarm_delta) {
	__u64 overruns = 0;

	if (ioctl(fd, LUX_IRQP_SET, &alarm_delta) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_IRQP_SET", pid);

	if (ioctl(fd, LUX_PIE_ON) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_PIE_ON", pid);

	for (int i = 0; i < 4; i++) {
		sleep(2); // Accumulate overruns
		if (read(fd, &overruns, sizeof(overruns)) < 0)
			err(EXIT_FAILURE, "[%d] read", pid);
		printf("[%d] Periodic alarm fired! Overruns: %llu, cause: %llu\n",
			   pid, LUX_CLOCK_OVERRUN(overruns), LUX_CLOCK_IRQ_CAUSE(overruns));
	}

	return overruns;
}

static int timer_test_alarm_async(int fd, int pid, __u64 alarm) {
	__u64 overruns = 0;
	const int sig = ALM_SIG;

	install_signal_handler(fd, pid);

	if (ioctl(fd, LUX_ALM_ASYNC_ON, &sig) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_ALM_ASYNC_ON", pid);

	if (ioctl(fd, LUX_ALM_SET, &alarm) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_ALM_SET", pid);
	printf("[%d] Alarm set to: %llu\n", pid, alarm);
	if (ioctl(fd, LUX_AIE_ON) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_AIE_ON", pid);

	if (ioctl(fd, LUX_ALM_RD, &alarm) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_ALM_RD", pid);
	printf("[%d] Alarm: %llu\n", pid, alarm);

	return overruns;
}

static int timer_test_alarm_periodic_async(int fd, int pid, __u64 alarm_delta) {
	__u64 overruns = 0;
	const int sig = ALM_SIG;

	if (ioctl(fd, LUX_ALM_ASYNC_ON, &sig) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_ALM_ASYNC_ON", pid);

	if (ioctl(fd, LUX_IRQP_SET, &alarm_delta) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_IRQP_SET", pid);

	if (ioctl(fd, LUX_PIE_ON) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_PIE_ON", pid);

	return overruns;
}

static void timer_test_async(int pid) {
	int fd = 0;
	__u64 now, alarm_delta, alarm, overruns;
	alarm_delta = LUX_TIMER_UNPRIV_MIN_DELTA / 2 + 50000;

	printf("[%d] Attempting to open /dev/lux-timer...\n", pid);

	fd = open("/dev/lux-timer", O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "[%d] open", pid);

	printf("[%d] Successfully opened /dev/lux-timer...\n", pid);

	now = timer_test_time(fd, pid);
	alarm = now + alarm_delta;

	timer_test_alarm_async(fd, pid, alarm);

	timer_test_alarm_periodic_async(fd, pid, alarm_delta);

	for (int i = 0; i < 5; i++) {
		sleep(2); // Accumulate overruns

		pause();

		if (read(fd, &overruns, sizeof(overruns)) < 0)
			err(EXIT_FAILURE, "[%d] read", pid);

		printf("[%d] Alarm fired! Overruns: %llu, cause: %s\n",
			   pid, LUX_CLOCK_OVERRUN(overruns), LUX_CLOCK_IRQ_CAUSE(overruns) == LUX_CLOCK_PERIODIC ? "PERIODIC" : "ALARM");
	}

	if (ioctl(fd, LUX_PIE_OFF) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_PIE_OFF", pid);
	if (ioctl(fd, LUX_AIE_OFF) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_AIE_OFF", pid);
	close(fd);

	printf("[%d] Closed /dev/lux-timer...\n", pid);
}

static void timer_test_sync(int pid) {
	int fd = 0;
	__u64 now, alarm_delta, alarm;
	alarm_delta = LUX_TIMER_UNPRIV_MIN_DELTA / 2 + 50000;

	printf("[%d] Attempting to open /dev/lux-timer...\n", pid);

	fd = open("/dev/lux-timer", O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "[%d] open", pid);

	printf("[%d] Successfully opened /dev/lux-timer...\n", pid);

	now = timer_test_time(fd, pid);
	alarm = now + alarm_delta;

	timer_test_alarm(fd, pid, alarm);

	timer_test_alarm_periodic(fd, pid, alarm_delta);

	if (ioctl(fd, LUX_PIE_OFF) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_PIE_OFF", pid);
	if (ioctl(fd, LUX_AIE_OFF) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_AIE_OFF", pid);
	close(fd);

	printf("[%d] Closed /dev/lux-timer...\n", pid);
}

int main(int argc, char *argv[]) {
	int child_nr = 0;

	if (argc < 2)
		print_usage_exit();

	child_nr = atoi(argv[1]);

	for (int i = 0; i < child_nr; i++) {
		int child_pid;

		child_pid = fork();
		if (child_pid != 0) // If in parent
			continue;		// Continue to fork the rest of the children

		// Child is here now; parent never comes here
		child_pid = getpid();
		printf("Child [%d] started...\n", child_pid);
		if (child_pid % 2)
			timer_test_sync(child_pid);
		else
			timer_test_async(child_pid);
		exit(EXIT_SUCCESS); // Child finished their job
	}

	// Parent comes here after spawning all children; children never come here (they exit before)
	// Wait until there are no children
	int wstatus;
	while (waitpid((pid_t)-1, &wstatus, WCONTINUED) > 0) {
		if (WIFEXITED(wstatus))
			printf("Child exited...status: %d\n", WEXITSTATUS(wstatus));
		else if (WIFSIGNALED(wstatus))
			printf("Child signaled...signal: %d\n", WTERMSIG(wstatus));
		else
			printf("Child continued...\n");
	}

	if (errno == ECHILD)
		printf("All children terminated...\n");
	else
		perror("waitpid");
}
