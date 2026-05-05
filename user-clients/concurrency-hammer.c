#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "mathaccel/include/uapi.h"

#define SHORT_OPTS "n:f:"
#define PREALLOCATE_NR 50
#define BUF_SIZ 255

static inline void print_usage_exit(void) {
	printf("Usage: concurrency-hammer -n <thread_no> -f <math_ops_file_path>\n");
	exit(EXIT_SUCCESS);
}

struct program_args {
	int thread_no;
	const char *math_op_file;
};

struct thread_context {
	int thread_id;
	int iterations;

	// Metrics
	unsigned long long min_latency;
	unsigned long long max_latency;
	unsigned long long total_latency;
	double mean_latency;
	double m2; // Sum of squares of differences from the current mean
	int success_count;
	int error_count;
};

struct req_res {
	struct mathaccel_req request;
	int64_t expected_result;
};

struct req_res *operations;

pthread_t *thread_pool;
struct thread_context *thread_contexts;
pthread_barrier_t thread_barrier;

static inline void parse_options(int argc, char *argv[], struct program_args *pa) {
	int c = 0, thread_no = 0;
	char *math_op_file = NULL;

	c = getopt(argc, argv, SHORT_OPTS);
	while (c != -1) {
		switch (c) {
			case '?':
			case ':':
				print_usage_exit();
				break;

			case 'n':
				thread_no = atoi(optarg);
				break;
			case 'f':
				math_op_file = optarg;
				break;
			default: // success
				print_usage_exit();
		}

		c = getopt(argc, argv, SHORT_OPTS);
	}

	printf("thread_no: %d, math ops file: %s\n", thread_no, math_op_file);

	pa->thread_no = thread_no;
	pa->math_op_file = math_op_file;
}

static inline void parse_line(char *curr_line, int num_op_parsed, struct req_res *current_operation) {
	char *curr_token = NULL;
	enum {
		STATE_INIT,
		STATE_PARSED_ARG1,
		STATE_PARSED_OP,
		STATE_PARSED_ARG2,
	} state = STATE_INIT;

	// Parse line tokens one by one (separated by " ")
	for (curr_token = strtok(curr_line, " ");
		 curr_token != NULL;
		 curr_token = strtok(NULL, " "))
		switch (state) {
			case STATE_INIT:
				current_operation->request.args[0] = atoi(curr_token);
				if (current_operation->request.args[0] == 0 && *curr_token != '0') // If couldn't convert to integer
					errx(EXIT_FAILURE,
						 "parse_operations_file: Failed to convert token '%s' at line %d to integer\n",
						 curr_token, num_op_parsed + 1);
				state = STATE_PARSED_ARG1;
				break;

			case STATE_PARSED_ARG1:
				switch (*curr_token) {
					case '+':
						current_operation->request.opcode = MATH_OP_ADD;
						break;

					case '-':
						current_operation->request.opcode = MATH_OP_SUB;
						break;

					case '*':
						current_operation->request.opcode = MATH_OP_MUL;
						break;

					case '/':
						current_operation->request.opcode = MATH_OP_DIV;
						break;

					default:
						errx(EXIT_FAILURE,
							 "parse_operations_file: Token %s at line %d is not an accepted operator\n",
							 curr_token, num_op_parsed + 1);
				}

				state = STATE_PARSED_OP;
				break;

			case STATE_PARSED_OP:
				current_operation->request.args[1] = atoi(curr_token);
				if (current_operation->request.args[1] == 0 && *curr_token != '0') // If couldn't convert to integer
					errx(EXIT_FAILURE,
						 "parse_operations_file: Failed to convert token '%s' at line %d to integer\n",
						 curr_token, num_op_parsed + 1);
				state = STATE_PARSED_ARG2;
				break;

			case STATE_PARSED_ARG2:
				current_operation->expected_result = atoi(curr_token);
				if (current_operation->expected_result == 0 && *curr_token != '0') // If couldn't convert to integer
					errx(EXIT_FAILURE,
						 "parse_operations_file: Failed to convert token '%s' at line %d to integer\n",
						 curr_token, num_op_parsed + 1);
				state = STATE_INIT;
				break;
		}
}

/* Parse file at @file_path results into the array @operations.
 * @param file_path The path to the file containing the operation spec
 * @param operations The output argument. It is allocated using malloc by the function.
 * @returns The number of allocated operations or an error value.
 * @note You must free() operations at the end.
 * */
static inline int parse_operations_file(const char *file_path) {
	// 1. Allocate array and open file
	int num_op_parsed = 0; // initial value
	int capacity = PREALLOCATE_NR;
	FILE *f = NULL;
	char curr_line[BUF_SIZ] = {0};

	operations = calloc(PREALLOCATE_NR, sizeof(*operations));
	if (operations == NULL)
		errx(EXIT_FAILURE, "parse_operations_file: Allocating operations array failed: %s\n",
			 strerror(errno));

	struct req_res *current_operation = operations;

	f = fopen(file_path, "r");
	if (f == NULL)
		errx(EXIT_FAILURE, "parse_operations_file: %s\n",
			 strerror(errno));

	// 2. Parse line by line
	while (fgets(curr_line, BUF_SIZ, f) != NULL) {
		parse_line(curr_line, num_op_parsed, current_operation);
		num_op_parsed++;

		if (num_op_parsed >= capacity) {
			capacity = num_op_parsed * 2;
			printf("parse_operations_file: Recalculating capacity (current: %d, new: %d)\n", num_op_parsed, capacity);
			if ((operations = reallocarray(operations, capacity, sizeof(*operations))) == NULL)
				errx(EXIT_FAILURE, "parse_operations_file: Reallocating operations array failed: %s\n",
					 strerror(errno));
		}

		current_operation = operations + num_op_parsed; // Important: operations could change, so can't simply use ++
	}

	return num_op_parsed;
}

static inline void print_operations(int count) {
	if (operations == NULL)
		errx(EXIT_FAILURE, "print_operations: Operation array is not allocated\n");

	static char opcode_to_operator[] = {
		[MATH_OP_ADD] = '+',
		[MATH_OP_SUB] = '-',
		[MATH_OP_MUL] = '*',
		[MATH_OP_DIV] = '/',
	};

	struct req_res *curr_op = operations;

	for (int i = 0; i < count; i++, curr_op++)
		printf("%d %c %d = %ld\n", curr_op->request.args[0],
			   opcode_to_operator[curr_op->request.opcode],
			   curr_op->request.args[1], curr_op->expected_result);
}

static inline long long calculate_latency_us(struct timespec *start_ts, struct timespec *end_ts) {
	// 1. Get differences
	long long seconds = end_ts->tv_sec - start_ts->tv_sec;
	long long nano_seconds = end_ts->tv_nsec - start_ts->tv_nsec;

	// 2. Normalize to both components microseconds
	long long normalized_seconds = seconds * 1000000LL;
	long long normalized_nano_seconds = nano_seconds / 1000LL;

	return normalized_seconds + normalized_nano_seconds;
}

static inline void calculate_iteration_stats(struct thread_context *ctx, int curr_iteration, struct timespec *start_ts, struct timespec *end_ts) {
	unsigned long long current_latency;
	double delta1, delta2;

	current_latency = calculate_latency_us(start_ts, end_ts);
	ctx->total_latency += current_latency;
	ctx->min_latency = current_latency < ctx->min_latency ? current_latency : ctx->min_latency;
	ctx->max_latency = current_latency > ctx->max_latency ? current_latency : ctx->max_latency;

	delta1 = current_latency - ctx->mean_latency;
	ctx->mean_latency += delta1 / curr_iteration;

	delta2 = current_latency - ctx->mean_latency;
	ctx->m2 += delta1 * delta2;
}

static inline double get_stddev(struct thread_context *ctx) {
	double variance = (ctx->iterations < 2) ? 0.0 : ctx->m2 / (ctx->iterations - 1);

	return sqrt(variance);
}

static void *thread_fn([[maybe_unused]] void *arg) {
	pthread_barrier_wait(&thread_barrier);

	int i = 0, fd, ret;
	struct thread_context *ctx = arg;
	ctx->min_latency = ULLONG_MAX; // Important for correctness of calculation

	char file_path[BUF_SIZ] = "/dev/mathaccel-x";
	struct timespec start_ts, end_ts;

	file_path[strlen(file_path) - 1] = '0' + (ctx->thread_id % 4);

	fd = open(file_path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "thread %d: open %s: %s\n", ctx->thread_id, file_path, strerror(errno));

		return NULL;
	}

	for (i = 0; i < ctx->iterations; i++) {
		struct req_res current_operation = *(operations + i);

		if (clock_gettime(CLOCK_MONOTONIC, &start_ts) == -1) {
			fprintf(stderr, "thread %d: clock_gettime: %s\n", ctx->thread_id, strerror(errno));
			return NULL;
		};

		ret = ioctl(fd, MATHACCEL_IOC_COMPUTE, &current_operation);
		if (ret < 0) {
			fprintf(stderr, "thread %d: ioctl: %s\n", ctx->thread_id, strerror(errno));
			ctx->error_count++;
			continue;
		}

		if (current_operation.expected_result != current_operation.request.result) {
			fprintf(stderr, "thread %d:Wrong result for request with cmd_id %d: expected %ld, got %lld\n",
					ctx->thread_id, current_operation.request.cmd_id, current_operation.expected_result, current_operation.request.result);

			ctx->error_count++;
		} else
			ctx->success_count++;

		if (clock_gettime(CLOCK_MONOTONIC, &end_ts) == -1) {
			fprintf(stderr, "thread %d: clock_gettime: %s\n", ctx->thread_id, strerror(errno));
			return NULL;
		};

		calculate_iteration_stats(ctx, i + 1, &start_ts, &end_ts);
	}

	return NULL;
}

static inline void print_thread_context_stats(struct thread_context *ctx) {
	printf("Reporting statistics for thread %d:\n", ctx->thread_id);

	printf("\tsuccess count: %d, error count: %d, total latency: %lld us\n",
		   ctx->success_count, ctx->error_count, ctx->total_latency);

	printf("\tmin latency: % lld us, max latency: % lld us\n",
		   ctx->min_latency, ctx->max_latency);

	printf("\tmean latency: %f us, stddev: %f us\n",
		   ctx->mean_latency, get_stddev(ctx));
}

int main(int argc, char *argv[]) {
	struct program_args prog_args = {};
	int i = 0, ret;

	if (argc < 3)
		print_usage_exit();

	parse_options(argc, argv, &prog_args);
	int op_count = parse_operations_file(prog_args.math_op_file);
	// print_operations(op_count);

	thread_pool = calloc(prog_args.thread_no, sizeof(pthread_t));
	if (thread_pool == NULL)
		errx(EXIT_FAILURE, "Failed to allocate thread pool: %s\n", strerror(errno));

	thread_contexts = calloc(prog_args.thread_no, sizeof(struct thread_context));
	if (thread_contexts == NULL)
		errx(EXIT_FAILURE, "Failed to allocate thread pool: %s\n", strerror(errno));

	pthread_barrier_init(&thread_barrier, NULL, prog_args.thread_no);
	for (i = 0; i < prog_args.thread_no; i++) {
		struct thread_context *curr_ctx = thread_contexts + i;
		curr_ctx->thread_id = i;
		curr_ctx->iterations = op_count;

		ret = pthread_create(thread_pool + i, NULL, thread_fn, curr_ctx);

		if (ret != 0)
			errx(EXIT_FAILURE, "Failed to create thread %d: %s\n", i, strerror(ret));
	}

	for (i = 0; i < prog_args.thread_no; i++) {
		ret = pthread_join(thread_pool[i], NULL);
		print_thread_context_stats(thread_contexts + i);

		if (ret != 0)
			fprintf(stderr, "Error while joining thread %d: %s\n", i, strerror(ret));
	}

	pthread_barrier_destroy(&thread_barrier);
	free(operations);
	return 0;
}
