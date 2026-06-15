#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../qemu-drivers/led-gpio/include/lux_ioctl.h"

static inline void print_usage_exit(void) {
	printf("Usage: timer-test <child-nr>\n");
	exit(EXIT_SUCCESS);
}

static void timer_test(int pid) {
	int fd = 0;
	__u64 now, alarm_delta, alarm, overruns;
	alarm_delta = LUX_TIMER_UNPRIV_MIN_DELTA / 2 + 50000;

	printf("[%d] Attempting to open /dev/lux-timer...\n", pid);

	fd = open("/dev/lux-timer", O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "[%d] open", pid);

	printf("[%d] Successfully opened /dev/lux-timer...\n", pid);

	if (ioctl(fd, LUX_TIME_RD, &now) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_TIME_RD", pid);

	printf("[%d] Time now: %llu\n", pid, now);

	now += 5000;
	if (ioctl(fd, LUX_TIME_SET, &now) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_TIME_SET", pid);
	printf("[%d] Time set to: %llu\n", pid, now);

	alarm = now + alarm_delta;
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

	if (ioctl(fd, LUX_IRQP_SET, &alarm_delta) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_IRQP_SET", pid);
	printf("[%d] Alarm set to: %llu\n", pid, alarm);
	if (ioctl(fd, LUX_PIE_ON) < 0)
		err(EXIT_FAILURE, "[%d] ioctl LUX_PIE_ON", pid);

	for (int i = 0; i < 4; i++) {
		sleep(2); // Accumulate overruns
		if (read(fd, &overruns, sizeof(overruns)) < 0)
			err(EXIT_FAILURE, "[%d] read", pid);
		printf("[%d] Periodic alarm fired! Overruns: %llu, cause: %llu\n",
			   pid, LUX_CLOCK_OVERRUN(overruns), LUX_CLOCK_IRQ_CAUSE(overruns));
	}

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
		printf("Child [%d] started...\n", getpid());
		timer_test(getpid());
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
