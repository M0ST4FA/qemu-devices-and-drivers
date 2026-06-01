CC := gcc

PROJ_PATH := /home/m0st4fa/workspace/kernel/experiments/devices
BUILD_DIR := ${PROJ_PATH}/build/user-clients

CFLAGS += -Wall -pedantic -std=c23 -g -Og -D_GNU_SOURCE -lm -pthread -I${PROJ_PATH}/qemu-drivers
LDFLAGS +=

TARGETS := edu mathaccel concurrency-hammer print-to-led-pwrite print-to-led-mmap print-to-led-sysfs print-to-mcled-sysfs

.PHONY: all clean ${TARGETS}

all: ${TARGETS}

edu: edu.c
	@mkdir -p ${BUILD_DIR}
	${CC} ${CFLAGS} -o ${BUILD_DIR}/$@ $< ${LDFLAGS}

mathaccel: mathaccel.c
	@mkdir -p ${BUILD_DIR}
	${CC} ${CFLAGS} -o ${BUILD_DIR}/$@ $< ${LDFLAGS}

concurrency-hammer: concurrency-hammer.c
	@mkdir -p ${BUILD_DIR}
	${CC} ${CFLAGS} -o ${BUILD_DIR}/$@ $< ${LDFLAGS}

print-to-led-pwrite: print-to-led-pwrite.c
	@mkdir -p ${BUILD_DIR}
	${CC} ${CFLAGS} -o ${BUILD_DIR}/$@ $< ${LDFLAGS}

print-to-led-mmap: print-to-led-mmap.c
	@mkdir -p ${BUILD_DIR}
	${CC} ${CFLAGS} -o ${BUILD_DIR}/$@ $< ${LDFLAGS}


print-to-led-sysfs: print-to-led-sysfs.c
	@mkdir -p ${BUILD_DIR}
	${CC} ${CFLAGS} -o ${BUILD_DIR}/$@ $< ${LDFLAGS}

print-to-mcled-sysfs: print-to-mcled-sysfs.c
	@mkdir -p ${BUILD_DIR}
	${CC} ${CFLAGS} -o ${BUILD_DIR}/$@ $< ${LDFLAGS}

clean:
	rm -rf ${BUILD_DIR}/*
