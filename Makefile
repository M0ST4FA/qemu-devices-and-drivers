PROJ_PATH := /home/m0st4fa/workspace/kernel/experiments/devices
BUILD_DIR := ${PROJ_PATH}/build
SUBDIRS := "qemu-drivers" "qemu-devices" "user-clients"

export CC := gcc
export CFLAGS += -Wall -Wextra -g -Og -I${PROJ_PATH}/qemu-drivers
export LDFLAGS +=

KBUILD_MAKEFILE := /home/m0st4fa/workspace/kernel/linux-build/amd64-debug/Makefile
MODULE_PATH := ${CURDIR}

MODULE_NAMES := $(filter-out modules,${MAKECMDGOALS})
DEVICE_NAMES := $(filter-out devices,${MAKECMDGOALS})
CLEAN_NAMES := $(filter-out all clean modules devices user-clients bear run-qemu run-qemu-debug,${MAKECMDGOALS})

.PHONY: all modules devices user-clients clean bear debug run-qemu run-qemu-debug

# My Makefile
all: modules devices user-clients

modules:
	@mkdir -p ${BUILD_DIR}/modules
	${MAKE} -C ./qemu-drivers BUILD_DIR=${BUILD_DIR}/modules modules SUB_TARGETS=${MODULE_NAMES}

devices:
	@mkdir -p ${BUILD_DIR}/devices
	${MAKE} -C ./qemu-devices BUILD_DIR=${BUILD_DIR}/devices all SUB_TARGETS=${DEVICE_NAMES}

user-clients:
	@mkdir -p ${BUILD_DIR}/user-clients
	${MAKE} -C ./user-clients BUILD_DIR=${BUILD_DIR}/user-clients all

clean:
	@rm -rf ${BUILD_DIR}/modules/*
	@rm -rf ${BUILD_DIR}/devices/*
	@rm -rf ${BUILD_DIR}/user-clients/*

debug:
	gdb ~/workspace/kernel/linux-build/amd64-debug/vmlinux.unstripped

run-qemu:
	./run-qemu.sh

run-qemu-debug:
	./run-qemu.sh debug

bear:
	make clean
	bear -- make

# Only create no-op rules for words that are NOT main targets.
# This prevents "overriding recipe" warnings for 'modules', 'clean', etc.
ARGS := $(filter-out all modules devices user-clients clean bear debug run-qemu run-qemu-debug,$(MAKECMDGOALS))

$(ARGS):
	@:
