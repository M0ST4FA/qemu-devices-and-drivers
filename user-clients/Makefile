CC := gcc

PROJ_PATH := /home/m0st4fa/workspace/kernel/experiments/devices
BUILD_DIR := ${PROJ_PATH}/build/user-clients

CFLAGS += -Wall -pedantic -std=c23 -g -Og -I${PROJ_PATH}/qemu-drivers
LDFLAGS +=

TARGETS := $(edu mathaccel)

.PHONY: all clean ${TARGETS}

all: mathaccel edu

edu: edu.c
	@mkdir -p ${BUILD_DIR}
	${CC} ${CFLAGS} -o ${BUILD_DIR}/$@ $< ${LDFLAGS}

mathaccel: mathaccel.c
	@mkdir -p ${BUILD_DIR}
	${CC} ${CFLAGS} -o ${BUILD_DIR}/$@ $< ${LDFLAGS}

clean:
	rm -rf ${BUILD_DIR}/*
