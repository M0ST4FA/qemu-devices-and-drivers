#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static inline void print_usage_exit(void) {
	printf("Usage: timer-test <child-nr>\n");
	exit(EXIT_SUCCESS);
}

static void timer_test(int pid) {
	int fd = 0;

	printf("[%d] Attempting to open /dev/lux-timer...\n", pid);

	fd = open("/dev/lux-timer", O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "[%d] open", pid);

	printf("[%d] Successfully opened /dev/lux-timer...\n", pid);

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
